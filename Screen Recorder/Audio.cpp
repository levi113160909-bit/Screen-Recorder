// =============================================================
//  Audio.cpp - أخذ الصوت من ويندوز (WASAPI)
// =============================================================
#include "Audio.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <propsys.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <future>
#include <vector>

#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // اسم الجهاز (متل "Speakers (Realtek)")
    const PROPERTYKEY kDeviceFriendlyName =
        { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

    constexpr size_t MAX_FIFO_FRAMES = AudioSource::SAMPLE_RATE * 2;   // أقصى شي ثانيتين
}

bool AudioSource::Start()
{
    if (m_running)
        return true;
    Stop();

    std::promise<bool> ready;
    std::future<bool>  result = ready.get_future();

    m_running = true;
    m_thread  = std::thread(&AudioSource::ThreadMain, this, &ready);

    const bool ok = result.get();      // منستنى لحتى نعرف إذا الجهاز اشتغل
    if (!ok)
        Stop();
    return ok;
}

void AudioSource::Stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    m_peak = 0.0f;
}

std::wstring AudioSource::DeviceName()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_name;
}

// -------------------------------------------------------------
// "الحنفيات" (Taps): كل مين بدو صوت (التسجيل، البث) إلو مخزن خاص فيه
// لحتى ما ياخدوا الصوت من بعض
// -------------------------------------------------------------
int AudioSource::AddTap()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const int id = m_nextTap++;
    m_taps[id] = Tap{};
    return id;
}

void AudioSource::RemoveTap(int tap)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_taps.erase(tap);
}

void AudioSource::SetArmed(int tap, bool armed, bool clear)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_taps.find(tap);
    if (it == m_taps.end())
        return;
    if (clear)
        it->second.fifo.clear();
    it->second.armed = armed;
}

size_t AudioSource::Read(int tap, float* dst, size_t frames)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_taps.find(tap);
    if (it == m_taps.end())
        return 0;
    std::deque<float>& fifo = it->second.fifo;
    const size_t have = fifo.size() / CHANNELS;
    const size_t n    = std::min(have, frames);
    std::copy(fifo.begin(), fifo.begin() + n * CHANNELS, dst);
    fifo.erase(fifo.begin(), fifo.begin() + n * CHANNELS);
    return n;
}

void AudioSource::KeepNewest(int tap, size_t frames)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_taps.find(tap);
    if (it == m_taps.end())
        return;
    std::deque<float>& fifo = it->second.fifo;
    const size_t have = fifo.size() / CHANNELS;
    if (have > frames)
        fifo.erase(fifo.begin(), fifo.begin() + (have - frames) * CHANNELS);
}

// -------------------------------------------------------------
// قراءة عينة وحدة وتحويلها لرقم بين -1 و 1
// -------------------------------------------------------------
float AudioSource::SampleAt(const BYTE* data, UINT32 frame, UINT32 channel) const
{
    const BYTE* p = data + (size_t)frame * m_srcBlock + (size_t)channel * (m_srcBits / 8);
    if (m_srcFloat)
        return *(const float*)p;

    switch (m_srcBits)
    {
    case 16: return *(const INT16*)p / 32768.0f;
    case 24: { const int v = (int)((UINT32)p[0] << 8 | (UINT32)p[1] << 16 | (UINT32)p[2] << 24) >> 8;
               return v / 8388608.0f; }
    case 32: return *(const INT32*)p / 2147483648.0f;
    }
    return 0.0f;
}

// -------------------------------------------------------------
// كل دفعة صوت جاية من الجهاز: منقيس مستواها، منحوّلها، ومنخزّنها
// -------------------------------------------------------------
void AudioSource::Process(const BYTE* data, UINT32 frames, bool silent)
{
    std::vector<float> out;
    out.reserve((size_t)(frames / m_step + 4) * CHANNELS);
    float peak = 0.0f;

    // علو الصوت: 0 = سكوت ، 1 = طبيعي ، 2 = ضعفين. والكتم = صفر
    const float gain = m_muted ? 0.0f : m_volume.load();

    for (UINT32 f = 0; f < frames; ++f)
    {
        float l = 0.0f, r = 0.0f;
        if (!silent)
        {
            l = SampleAt(data, f, 0) * gain;
            r = (m_srcChannels > 1 ? SampleAt(data, f, 1) : SampleAt(data, f, 0)) * gain;   // مايك مونو -> منكرّر القناة
        }
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));

        // تحويل السرعة لـ 48000 (بالتوسيط بين كل عينتين)
        while (m_phase < 1.0)
        {
            out.push_back(m_prevL + (l - m_prevL) * (float)m_phase);
            out.push_back(m_prevR + (r - m_prevR) * (float)m_phase);
            m_phase += m_step;
        }
        m_phase -= 1.0;
        m_prevL = l;
        m_prevR = r;
    }

    if (peak > m_peak.load())
        m_peak = peak;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, tap] : m_taps)
    {
        if (!tap.armed)
            continue;
        tap.fifo.insert(tap.fifo.end(), out.begin(), out.end());
        const size_t have = tap.fifo.size() / CHANNELS;
        if (have > MAX_FIFO_FRAMES)
            tap.fifo.erase(tap.fifo.begin(), tap.fifo.begin() + (have - MAX_FIFO_FRAMES) * CHANNELS);
    }
}

// -------------------------------------------------------------
// قائمة الأجهزة
// -------------------------------------------------------------
std::vector<AudioDevice> AudioSource::ListDevices(Kind kind)
{
    std::vector<AudioDevice> list;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return list;
    if (FAILED(enumerator->EnumAudioEndpoints(kind == Kind::System ? eRender : eCapture, DEVICE_STATE_ACTIVE, &devices)))
        return list;

    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(i, &device)))
            continue;

        AudioDevice d;
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id)))
        {
            d.id = id;
            CoTaskMemFree(id);
        }

        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)))
        {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(kDeviceFriendlyName, &pv)) && pv.vt == VT_LPWSTR)
                d.name = pv.pwszVal;
            PropVariantClear(&pv);
        }
        if (!d.id.empty())
            list.push_back(d);
    }
    return list;
}

// -------------------------------------------------------------
// الخيط: بيفتح الجهاز وبيضل ياخد صوت كل 10 ملي ثانية
// -------------------------------------------------------------
void AudioSource::ThreadMain(std::promise<bool>* ready)
{
    bool readySent = false;
    auto sendReady = [&](bool ok) { if (!readySent) { ready->set_value(ok); readySent = true; } };

    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice>           device;
        ComPtr<IAudioClient>        client;
        ComPtr<IAudioCaptureClient> capture;
        WAVEFORMATEX*               mix = nullptr;
        const bool loopback = (m_kind == Kind::System);

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));

        // صوت النظام = جهاز الإخراج (السماعات) ، الميكروفون = جهاز الإدخال
        // إذا المستخدم اختار جهاز معيّن منفتحه، وإلا الافتراضي
        if (SUCCEEDED(hr) && !m_deviceId.empty())
        {
            if (FAILED(enumerator->GetDevice(m_deviceId.c_str(), &device)))
                device.Reset();          // الجهاز انفصل: منرجع للافتراضي
        }
        if (SUCCEEDED(hr) && !device)
            hr = enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &device);

        if (SUCCEEDED(hr))
        {
            ComPtr<IPropertyStore> props;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)))
            {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                if (SUCCEEDED(props->GetValue(kDeviceFriendlyName, &pv)) && pv.vt == VT_LPWSTR)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_name = pv.pwszVal;
                }
                PropVariantClear(&pv);
            }
        }

        if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.GetAddressOf());
        if (SUCCEEDED(hr)) hr = client->GetMixFormat(&mix);

        if (SUCCEEDED(hr))
        {
            // LOOPBACK = "سجّل اللي طالع من السماعات"
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
                                    2'000'000 /* 200ms */, 0, mix, nullptr);
        }
        if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));

        if (SUCCEEDED(hr))
        {
            // منفهم شكل الصوت (عدد القنوات، السرعة، نوع الأرقام)
            m_srcChannels = mix->nChannels;
            m_srcRate     = mix->nSamplesPerSec;
            m_srcBits     = mix->wBitsPerSample;
            m_srcBlock    = mix->nBlockAlign;
            WORD tag      = mix->wFormatTag;
            if (tag == WAVE_FORMAT_EXTENSIBLE)
                tag = (WORD)((WAVEFORMATEXTENSIBLE*)mix)->SubFormat.Data1;
            m_srcFloat = (tag == WAVE_FORMAT_IEEE_FLOAT);

            const bool supported = (m_srcFloat && m_srcBits == 32) ||
                                   (tag == WAVE_FORMAT_PCM && (m_srcBits == 16 || m_srcBits == 24 || m_srcBits == 32));
            if (!supported || m_srcChannels == 0)
                hr = E_NOTIMPL;

            m_step  = (double)m_srcRate / SAMPLE_RATE;
            m_phase = 0.0;
            m_prevL = m_prevR = 0.0f;
        }

        if (SUCCEEDED(hr)) hr = client->Start();

        sendReady(SUCCEEDED(hr));

        // الحلقة: كل 10 ملي ثانية منشوف إذا في صوت جديد
        while (SUCCEEDED(hr) && m_running)
        {
            Sleep(10);
            UINT32 packet = 0;
            hr = capture->GetNextPacketSize(&packet);
            while (SUCCEEDED(hr) && packet > 0)
            {
                BYTE*  data   = nullptr;
                UINT32 frames = 0;
                DWORD  flags  = 0;
                hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr))
                    break;
                Process(data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
                capture->ReleaseBuffer(frames);
                hr = capture->GetNextPacketSize(&packet);
            }
        }

        if (client)
            client->Stop();
        if (mix)
            CoTaskMemFree(mix);
    }
    sendReady(false);
    m_running = false;

    if (SUCCEEDED(hrCom))
        CoUninitialize();
}
