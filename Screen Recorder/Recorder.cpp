// =============================================================
//  Recorder.cpp - هون بيصير التسجيل الحقيقي
// =============================================================
#include "Recorder.h"

#include <mmsystem.h>      // timeBeginPeriod: توقيت أدق
#include <mfapi.h>
#include <mfidl.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "winmm.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // Media Foundation بتحسب الوقت بوحدة 100 نانو ثانية
    constexpr LONGLONG ONE_SECOND     = 10'000'000;

    // الصوت بيتأخر 100ms عن الوقت الحقيقي، لحتى يكون في وقت يوصل من الجهاز
    constexpr size_t AUDIO_LAG_FRAMES = AudioSource::SAMPLE_RATE / 10;

    LONGLONG QpcNow()
    {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return t.QuadPart;
    }

    // ---------------------------------------------------------
    // منجهّز "الكاتب": ملف MP4 فيه فيديو H.264 + صوت AAC
    // ---------------------------------------------------------
    HRESULT CreateWriter(const std::wstring& path, UINT32 width, UINT32 height, UINT32 fps, UINT32 bitrate,
                         bool withAudio, bool useHardware, bool crashSafe,
                         IMFSinkWriter** ppWriter, DWORD* pVideo, DWORD* pAudio)
    {
        ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 3);
        // MP4 "مقطّع": بيكتب الفيديو على شكل قطع صغيرة، فإذا البرنامج ضرب
        // أو انطفى الجهاز بيضل كل شي قبلها شغّال
        if (SUCCEEDED(hr) && crashSafe)
            hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_FMPEG4);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, useHardware);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);

        ComPtr<IMFSinkWriter> writer;
        if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes.Get(), &writer);

        // ---------- الفيديو ----------
        ComPtr<IMFMediaType> vOut;
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&vOut);
        if (SUCCEEDED(hr)) hr = vOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = vOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (SUCCEEDED(hr)) hr = vOut->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        if (SUCCEEDED(hr)) hr = vOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(vOut.Get(), MF_MT_FRAME_SIZE, width, height);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(vOut.Get(), MF_MT_FRAME_RATE, fps, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(vOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        DWORD video = 0;
        if (SUCCEEDED(hr)) hr = writer->AddStream(vOut.Get(), &video);

        ComPtr<IMFMediaType> vIn;   // اللي منعطيه: صور RGB32 من تحت لفوق
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&vIn);
        if (SUCCEEDED(hr)) hr = vIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = vIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = vIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = vIn->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(-(LONG)(width * 4)));
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(vIn.Get(), MF_MT_FRAME_SIZE, width, height);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(vIn.Get(), MF_MT_FRAME_RATE, fps, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(vIn.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(video, vIn.Get(), nullptr);

        // ---------- الصوت ----------
        DWORD audio = 0;
        if (withAudio)
        {
            ComPtr<IMFMediaType> aOut;   // AAC: 48000، ستيريو، 192 كيلوبت
            if (SUCCEEDED(hr)) hr = MFCreateMediaType(&aOut);
            if (SUCCEEDED(hr)) hr = aOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = aOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            if (SUCCEEDED(hr)) hr = aOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = aOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AudioSource::SAMPLE_RATE);
            if (SUCCEEDED(hr)) hr = aOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, AudioSource::CHANNELS);
            if (SUCCEEDED(hr)) hr = aOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
            if (SUCCEEDED(hr)) hr = writer->AddStream(aOut.Get(), &audio);

            ComPtr<IMFMediaType> aIn;    // اللي منعطيه: PCM 16 بت
            if (SUCCEEDED(hr)) hr = MFCreateMediaType(&aIn);
            if (SUCCEEDED(hr)) hr = aIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = aIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AudioSource::SAMPLE_RATE);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, AudioSource::CHANNELS);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, AudioSource::CHANNELS * 2);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AudioSource::SAMPLE_RATE * AudioSource::CHANNELS * 2);
            if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audio, aIn.Get(), nullptr);
        }

        if (SUCCEEDED(hr)) hr = writer->BeginWriting();
        if (SUCCEEDED(hr))
        {
            *ppWriter = writer.Detach();
            *pVideo = video;
            *pAudio = audio;
        }
        return hr;
    }

    // بعد ما يشتغل الكاتب، منسأله: مين المشفّر اللي اختاره فعلاً؟
    std::wstring QueryEncoderName(IMFSinkWriter* writer, DWORD stream)
    {
        ComPtr<IMFSinkWriterEx> ex;
        if (FAILED(writer->QueryInterface(IID_PPV_ARGS(&ex))))
            return L"";

        for (DWORD i = 0;; ++i)
        {
            GUID category{};
            ComPtr<IMFTransform> transform;
            if (FAILED(ex->GetTransformForStream(stream, i, &category, &transform)))
                break;
            if (category != MFT_CATEGORY_VIDEO_ENCODER)
                continue;

            ComPtr<IMFAttributes> attrs;
            if (SUCCEEDED(transform->GetAttributes(&attrs)))
            {
                WCHAR* name = nullptr;
                UINT32 len = 0;
                if (SUCCEEDED(attrs->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &len)))
                {
                    std::wstring result = name;
                    CoTaskMemFree(name);
                    return result;
                }
            }
            return L"Microsoft H.264 (CPU)";   // المشفّر البرمجي ما إلو اسم
        }
        return L"";
    }
}

std::vector<Recorder::EncoderInfo> Recorder::ListEncoders()
{
    std::vector<EncoderInfo> list;
    MFT_REGISTER_TYPE_INFO output{ MFMediaType_Video, MFVideoFormat_H264 };

    // مرتين: أول شي بطاقات الرسومات، بعدين البرمجي (المعالج)
    for (int pass = 0; pass < 2; ++pass)
    {
        const UINT32 flags = (pass == 0 ? MFT_ENUM_FLAG_HARDWARE : (MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT))
                           | MFT_ENUM_FLAG_SORTANDFILTER;
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, nullptr, &output, &activates, &count)))
            continue;

        for (UINT32 i = 0; i < count; ++i)
        {
            WCHAR* name = nullptr;
            UINT32 len = 0;
            if (SUCCEEDED(activates[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &len)))
            {
                list.push_back({ name, pass == 0 });
                CoTaskMemFree(name);
            }
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
    }
    return list;
}

// =============================================================
//  البداية والنهاية
// =============================================================
HRESULT Recorder::Start(const RecordOptions& options)
{
    Stop();
    m_opt = options;

    if (!m_opt.scene)
        return E_INVALIDARG;
    {
        std::lock_guard<std::mutex> lock(m_opt.scene->mutex);
        m_width  = m_opt.scene->canvasW & ~1;      // H.264 بدها أرقام زوجية
        m_height = m_opt.scene->canvasH & ~1;
    }
    if (m_width < 16 || m_height < 16)
        return E_INVALIDARG;

    m_hasAudio      = !m_opt.audio.empty();
    m_fps           = std::clamp<UINT32>(m_opt.fps, 1, 120);
    m_frameDuration = ONE_SECOND / m_fps;
    const UINT32 bitrate = std::clamp<UINT32>(m_opt.bitrate, 500'000, 200'000'000);

    // حسب نوع المشفّر المختار
    const bool tryHardware = (m_opt.encoder != EncoderMode::Software);
    HRESULT hr = CreateWriter(m_opt.path, m_width, m_height, m_fps, bitrate, m_hasAudio, tryHardware, m_opt.crashSafe,
                              &m_writer, &m_videoStream, &m_audioStream);
    if (FAILED(hr) && m_opt.encoder == EncoderMode::Auto)   // تلقائي: إذا كرت الشاشة ما زبط منجرّب المعالج
    {
        m_writer.Reset();
        hr = CreateWriter(m_opt.path, m_width, m_height, m_fps, bitrate, m_hasAudio, false, m_opt.crashSafe,
                          &m_writer, &m_videoStream, &m_audioStream);
    }
    if (FAILED(hr))
    {
        m_writer.Reset();
        DeleteFileW(m_opt.path.c_str());
        return hr;
    }

    m_encoderName = QueryEncoderName(m_writer.Get(), m_videoStream);

    // الساعة
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    m_freq        = f.QuadPart;
    m_start       = QpcNow();
    m_pausedTotal = 0;
    m_paused      = false;
    m_error       = S_OK;

    for (AudioSource* src : m_opt.audio)
        src->SetArmed(true, true);       // بلّش خزّن صوت للتسجيل

    m_active  = true;
    m_running = true;
    m_videoThread = std::thread(&Recorder::VideoThread, this);
    if (m_hasAudio)
        m_audioThread = std::thread(&Recorder::AudioThread, this);
    return S_OK;
}

HRESULT Recorder::Stop()
{
    if (!m_active)
        return S_OK;

    m_running = false;
    if (m_videoThread.joinable()) m_videoThread.join();
    if (m_audioThread.joinable()) m_audioThread.join();

    for (AudioSource* src : m_opt.audio)
        src->SetArmed(false, true);

    // منسكّر الملف صح (بدون هالخطوة الفيديو بيخرب)
    HRESULT hr = m_error;
    if (m_writer)
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        const HRESULT hrFinal = m_writer->Finalize();
        if (SUCCEEDED(hr)) hr = hrFinal;
        m_writer.Reset();
    }

    m_active = false;
    m_paused = false;
    m_opt.audio.clear();
    return hr;
}

void Recorder::SetPaused(bool paused)
{
    if (!m_active)
        return;
    {
        std::lock_guard<std::mutex> lock(m_clockMutex);
        if (paused == m_paused)
            return;
        if (paused)
            m_pauseStart = QpcNow();
        else
            m_pausedTotal += QpcNow() - m_pauseStart;   // منشيل وقت الإيقاف من الحساب
        m_paused = paused;
    }
    // وقت الإيقاف المؤقت ما منخزّن صوت
    for (AudioSource* src : m_opt.audio)
        src->SetArmed(!paused, false);
}

LONGLONG Recorder::Elapsed100ns()
{
    std::lock_guard<std::mutex> lock(m_clockMutex);
    if (!m_active)
        return 0;
    const LONGLONG now   = m_paused ? m_pauseStart : QpcNow();
    LONGLONG       ticks = now - m_start - m_pausedTotal;
    if (ticks < 0) ticks = 0;
    return (ticks / m_freq) * ONE_SECOND + (ticks % m_freq) * ONE_SECOND / m_freq;
}

// =============================================================
//  الكتابة بالملف
// =============================================================
HRESULT Recorder::WriteVideo(const void* pixels, LONGLONG time)
{
    const DWORD bytes = (DWORD)m_width * m_height * 4;
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);

    BYTE* dest = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&dest, nullptr, nullptr);
    if (SUCCEEDED(hr))
    {
        memcpy(dest, pixels, bytes);
        buffer->Unlock();
        hr = buffer->SetCurrentLength(bytes);
    }

    ComPtr<IMFSample> sample;
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(time);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(m_frameDuration);
    if (SUCCEEDED(hr))
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        hr = m_writer->WriteSample(m_videoStream, sample.Get());
    }
    return hr;
}

HRESULT Recorder::WriteAudio(const float* mix, size_t frames, LONGLONG time)
{
    const DWORD bytes = (DWORD)(frames * AudioSource::CHANNELS * sizeof(INT16));
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);

    BYTE* dest = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&dest, nullptr, nullptr);
    if (SUCCEEDED(hr))
    {
        // من أرقام عشرية (-1..1) لأرقام 16 بت
        INT16* out = (INT16*)dest;
        for (size_t i = 0; i < frames * AudioSource::CHANNELS; ++i)
        {
            const float v = std::clamp(mix[i], -1.0f, 1.0f);
            out[i] = (INT16)(v * 32767.0f);
        }
        buffer->Unlock();
        hr = buffer->SetCurrentLength(bytes);
    }

    ComPtr<IMFSample> sample;
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(time);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration((LONGLONG)frames * ONE_SECOND / AudioSource::SAMPLE_RATE);
    if (SUCCEEDED(hr))
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        hr = m_writer->WriteSample(m_audioStream, sample.Get());
    }
    return hr;
}

// =============================================================
//  خيط الصورة
// =============================================================
void Recorder::VideoThread()
{
    // WinRT (محرّك كرت الشاشة) بدها COM مفعّل بهالخيط
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // الصورة بالذاكرة: BGRA ، من تحت لفوق (متل ما بدها Media Foundation)
    std::vector<BYTE> frame((size_t)m_width * m_height * 4);
    HRESULT hr = S_OK;
    bool endedByItself = false;

    {
        Compositor compositor(true);           // true = منرسم الماوس
        std::vector<SceneLayer> layers;
        int canvasW = 0, canvasH = 0;

        // تسخين: أول كم صورة من ويندوز بتاخد شوي وقت لتوصل
        for (int i = 0; i < 3; ++i)
        {
            m_opt.scene->Snapshot(canvasW, canvasH, layers);
            compositor.Compose(layers, canvasW, canvasH, frame.data(), m_width, m_height, m_width * 4, true);
            Sleep(20);
        }
        m_usingGpu = compositor.UsingGpu();

        LONGLONG last = -1;
        timeBeginPeriod(1);
        while (m_running)
        {
            if (m_paused)               // إيقاف مؤقت: ما منكتب شي
            {
                Sleep(10);
                continue;
            }

            // نسخة من المشهد (يمكن المستخدم عم يحرّك الطبقات هلق)
            // وإذا تغيّر حجم اللوحة منصغّرها/منكبّرها لحجم الفيديو
            m_opt.scene->Snapshot(canvasW, canvasH, layers);
            compositor.Compose(layers, canvasW, canvasH, frame.data(), m_width, m_height, m_width * 4, true);
            m_usingGpu = compositor.UsingGpu();

            LONGLONG t = Elapsed100ns();
            if (t <= last) t = last + 1;
            hr = WriteVideo(frame.data(), t);
            if (FAILED(hr))
            {
                endedByItself = true;
                break;
            }
            last = t;

            // منستنى لوقت الصورة الجاية
            const LONGLONG next = (t / m_frameDuration + 1) * m_frameDuration;
            const LONGLONG now  = Elapsed100ns();
            if (next > now)
                Sleep((DWORD)((next - now) / 10'000));
        }
        timeEndPeriod(1);
    }

    if (SUCCEEDED(hrCom))
        CoUninitialize();

    if (endedByItself)
    {
        m_error = hr;
        m_running = false;
        PostMessageW(m_opt.notify, WM_RECORDER_DONE, (WPARAM)hr, 0);
    }
}

// =============================================================
//  خيط الصوت: بيخلط المصادر وبيكتب كل 10 ملي ثانية
// =============================================================
void Recorder::AudioThread()
{
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    LONGLONG written = 0;                         // كم عينة كتبنا لهلق
    std::vector<float> mix, tmp;

    while (m_running)
    {
        Sleep(10);

        // كم لازم يكون مكتوب لهلق (متأخرين 100ms عن قصد)
        const LONGLONG elapsedFrames = Elapsed100ns() * AudioSource::SAMPLE_RATE / ONE_SECOND;
        const LONGLONG target        = elapsedFrames - (LONGLONG)AUDIO_LAG_FRAMES;
        if (target <= written)
            continue;

        const size_t frames = (size_t)std::min<LONGLONG>(target - written, AudioSource::SAMPLE_RATE);
        mix.assign(frames * AudioSource::CHANNELS, 0.0f);
        tmp.resize(frames * AudioSource::CHANNELS);

        // الخلط = جمع الأصوات فوق بعض
        for (AudioSource* src : m_opt.audio)
        {
            const size_t got = src->Read(tmp.data(), frames);
            for (size_t i = 0; i < got * AudioSource::CHANNELS; ++i)
                mix[i] += tmp[i];

            // إذا تجمّع صوت أكتر من اللازم (الساعات مش متطابقة 100%) منرمي القديم
            src->KeepNewest(AUDIO_LAG_FRAMES * 3);
        }

        const LONGLONG time = written * ONE_SECOND / AudioSource::SAMPLE_RATE;
        const HRESULT hr = WriteAudio(mix.data(), frames, time);
        if (FAILED(hr))
            break;
        written += frames;
    }

    if (SUCCEEDED(hrCom))
        CoUninitialize();
}
