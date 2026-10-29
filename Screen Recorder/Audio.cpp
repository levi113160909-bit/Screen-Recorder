// =============================================================
//  Audio.cpp - أخذ الصوت من ويندوز (WASAPI)
// =============================================================
#include "Audio.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <audioclientactivationparams.h>
#include <wrl/implements.h>
#include <propsys.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <future>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // اسم الجهاز (متل "Speakers (Realtek)")
    const PROPERTYKEY kDeviceFriendlyName =
        { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

    constexpr size_t MAX_FIFO_FRAMES = AudioSource::SAMPLE_RATE * 2;   // أقصى شي ثانيتين

    // ---------------------------------------------------------
    // صوت برنامج واحد: ويندوز بيفتحه "بشكل غير متزامن"،
    // فمنعطيه هالكائن، وهو بيخبرنا لما يخلص
    // ---------------------------------------------------------
    using namespace Microsoft::WRL;
    class ActivateHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
                                                IActivateAudioInterfaceCompletionHandler>
    {
    public:
        ActivateHandler()  { done = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
        ~ActivateHandler() { if (done) CloseHandle(done); }

        STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* op) override
        {
            HRESULT activated = E_FAIL;
            ComPtr<IUnknown> unk;
            result = op->GetActivateResult(&activated, &unk);
            if (SUCCEEDED(result)) result = activated;
            if (SUCCEEDED(result)) result = unk.As(&client);
            SetEvent(done);
            return S_OK;
        }

        HANDLE               done = nullptr;
        HRESULT              result = E_FAIL;
        ComPtr<IAudioClient> client;
    };

    // بيفتح "مسجّل" لصوت برنامج واحد (ومعه البرامج اللي هو فتحها، متل كروم)
    HRESULT ActivateProcessLoopback(DWORD pid, ComPtr<IAudioClient>& client)
    {
        AUDIOCLIENT_ACTIVATION_PARAMS params{};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId     = pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

        PROPVARIANT pv{};
        pv.vt             = VT_BLOB;
        pv.blob.cbSize    = sizeof(params);
        pv.blob.pBlobData = (BYTE*)&params;

        ComPtr<ActivateHandler> handler = Make<ActivateHandler>();
        if (!handler || !handler->done)
            return E_OUTOFMEMORY;
        ComPtr<IActivateAudioInterfaceAsyncOperation> op;
        HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                                 &pv, handler.Get(), &op);
        if (FAILED(hr))
            return hr;
        if (WaitForSingleObject(handler->done, 5000) != WAIT_OBJECT_0)
            return E_ABORT;
        if (SUCCEEDED(handler->result))
            client = handler->client;
        return handler->result;
    }
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

size_t AudioSource::Available(int tap)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_taps.find(tap);
    return it == m_taps.end() ? 0 : it->second.fifo.size() / CHANNELS;
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

    for (UINT32 f = 0; f < frames; ++f)
    {
        float l = 0.0f, r = 0.0f;
        if (!silent)
        {
            l = SampleAt(data, f, 0);
            r = m_srcChannels > 1 ? SampleAt(data, f, 1) : l;   // مايك مونو -> منكرّر القناة
        }

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

    Deliver(out);
}

// فلاتر + علو + توزيع الصوت على كل الحنفيات
void AudioSource::Deliver(std::vector<float>& out)
{
    // الفلاتر (قبل العلو، لحتى البوابة تشتغل على صوتك الحقيقي)
    m_filters.Process(out.data(), out.size() / CHANNELS);

    // علو الصوت: 0 = سكوت ، 1 = طبيعي ، 2 = ضعفين. والكتم = صفر
    const float gain = m_muted ? 0.0f : m_volume.load();
    float peak = 0.0f;
    for (float& v : out)
    {
        v *= gain;
        peak = std::max(peak, std::fabs(v));
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

    if (m_kind == Kind::Generated)          // ما في جهاز: منصنع الصوت بنفسنا
    {
        sendReady(true);
        GeneratorLoop();
    }
    else if (m_kind == Kind::Application)   // صوت برنامج واحد
    {
        ComPtr<IAudioClient>        client;
        ComPtr<IAudioCaptureClient> capture;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        HRESULT hr = m_pid ? ActivateProcessLoopback(m_pid, client) : E_INVALIDARG;
        if (SUCCEEDED(hr))
        {
            // هون منحن منختار الشكل: 48000، ستيريو، أرقام عشرية (ويندوز بيحوّل لحالو)
            WAVEFORMATEX fmt{};
            fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
            fmt.nChannels       = 2;
            fmt.nSamplesPerSec  = SAMPLE_RATE;
            fmt.wBitsPerSample  = 32;
            fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
            fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                    2'000'000, 0, &fmt, nullptr);
        }
        if (SUCCEEDED(hr)) hr = client->SetEventHandle(event);
        if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
        if (SUCCEEDED(hr))
        {
            m_srcChannels = 2; m_srcRate = SAMPLE_RATE; m_srcBits = 32; m_srcBlock = 8; m_srcFloat = true;
            m_step = 1.0; m_phase = 0.0; m_prevL = m_prevR = 0.0f;
            hr = client->Start();
        }
        sendReady(SUCCEEDED(hr));

        while (SUCCEEDED(hr) && m_running)
        {
            WaitForSingleObject(event, 20);    // ويندوز بينبّهنا لما يجي صوت
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
        if (event)
            CloseHandle(event);
    }
    else
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

// -------------------------------------------------------------
// النوع Generated: كل 10 ملي ثانية منطلب من الدالة صوت بقدّ الوقت اللي مرق
// -------------------------------------------------------------
void AudioSource::GeneratorLoop()
{
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    LONGLONG produced = 0;
    std::vector<float> out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_name.empty())
            m_name = L"صوت مُولَّد";
    }

    while (m_running)
    {
        Sleep(10);
        QueryPerformanceCounter(&now);
        const LONGLONG due = (now.QuadPart - start.QuadPart) * SAMPLE_RATE / freq.QuadPart;
        LONGLONG frames = due - produced;
        if (frames <= 0)
            continue;
        if (frames > SAMPLE_RATE)          // الجهاز علّق كتير: ما منعوّض أكتر من ثانية
        {
            produced = due - SAMPLE_RATE;
            frames   = SAMPLE_RATE;
        }
        out.assign((size_t)frames * CHANNELS, 0.0f);
        if (m_generator)
            m_generator(out.data(), (size_t)frames);
        produced += frames;
        Deliver(out);
    }
}
