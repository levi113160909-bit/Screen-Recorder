// =============================================================
//  Streamer.cpp - البث المباشر
//
//   خيط الصورة: المشهد -> NV12 -> H.264 -> كل المنصات
//   خيط الصوت : خلط المصادر -> AAC -> كل المنصات
// =============================================================
#include "Streamer.h"
#include <algorithm>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace
{
    constexpr LONGLONG ONE_SECOND       = 10'000'000;
    constexpr size_t   AUDIO_LAG_FRAMES = AudioSource::SAMPLE_RATE / 10;   // 100ms
    constexpr size_t   AAC_FRAME        = 1024;

    LONGLONG QpcNow()
    {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return t.QuadPart;
    }
}

bool Streamer::Start(const StreamOptions& options, std::wstring& error)
{
    Stop();
    m_opt = options;

    if (!m_opt.scene)
    {
        error = L"لا يوجد مشهد";
        return false;
    }
    if (m_opt.targets.empty())
    {
        error = L"فعّل وجهة بث واحدة على الأقل";
        return false;
    }
    for (const StreamTarget& t : m_opt.targets)
    {
        if (t.key.empty())
        {
            error = L"أدخل مفتاح البث لـ " + t.name;
            return false;
        }
        if (t.url.empty())
        {
            error = L"أدخل عنوان الخادم لـ " + t.name;
            return false;
        }
    }

    m_width  = std::max<UINT32>(64, m_opt.width & ~1u);
    m_height = std::max<UINT32>(64, m_opt.height & ~1u);
    const UINT32 fps = std::clamp<UINT32>(m_opt.fps, 10, 60);
    m_opt.fps = fps;
    m_frameDuration = ONE_SECOND / fps;

    // المشفّرات
    const bool tryHw   = m_opt.encoder != EncoderMode::Software;
    const bool allowSw = true;
    if (FAILED(m_video.Init(m_width, m_height, fps, m_opt.videoKbps, tryHw, allowSw)))
    {
        error = L"تعذّر تشغيل مشفّر الفيديو";
        return false;
    }
    m_encoderName = m_video.Name();
    if (FAILED(m_audio.Init(m_opt.audioKbps)))
    {
        m_video.Shutdown();
        error = L"تعذّر تشغيل مشفّر الصوت";
        return false;
    }

    // المُرسِلات: وحدة لكل منصة
    m_shared = std::make_shared<Shared>();
    std::shared_ptr<Shared> shared = m_shared;
    StreamInfo info;
    info.width = m_width; info.height = m_height; info.fps = fps;
    info.videoKbps = m_opt.videoKbps; info.audioKbps = m_opt.audioKbps;
    for (const StreamTarget& t : m_opt.targets)
    {
        m_outputs.push_back(std::make_unique<RtmpOutput>(
            t, info,
            [shared] { std::lock_guard<std::mutex> lock(shared->mutex); return shared->avcConfig; },
            [shared] { shared->forceKeyframe = true; }));
    }

    // الصوت
    m_taps.clear();
    for (AudioSource* src : m_opt.audio)
    {
        const int tap = src->AddTap();
        src->SetArmed(tap, true, true);
        m_taps.push_back(tap);
    }

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    m_freq  = f.QuadPart;
    m_start = QpcNow();

    m_running = true;
    m_active  = true;
    for (auto& o : m_outputs)
        o->Start();
    m_videoThread = std::thread(&Streamer::VideoThread, this);
    m_audioThread = std::thread(&Streamer::AudioThread, this);
    return true;
}

void Streamer::Stop()
{
    if (!m_active)
        return;
    m_running = false;
    if (m_videoThread.joinable()) m_videoThread.join();
    if (m_audioThread.joinable()) m_audioThread.join();

    for (size_t i = 0; i < m_opt.audio.size() && i < m_taps.size(); ++i)
        m_opt.audio[i]->RemoveTap(m_taps[i]);
    m_taps.clear();

    // سكّرة الاتصالات ممكن تاخد شوي (نت بطيء) - منعملها بالخلفية
    // لحتى ما تعلق الواجهة
    auto outputs = std::make_shared<std::vector<std::unique_ptr<RtmpOutput>>>(std::move(m_outputs));
    m_outputs.clear();
    std::thread([outputs] { outputs->clear(); }).detach();

    m_video.Shutdown();
    m_audio.Shutdown();
    m_active = false;
}

LONGLONG Streamer::Elapsed100ns()
{
    if (!m_active)
        return 0;
    const LONGLONG ticks = QpcNow() - m_start;
    return (ticks / m_freq) * ONE_SECOND + (ticks % m_freq) * ONE_SECOND / m_freq;
}

LONGLONG Streamer::ElapsedMs()
{
    return Elapsed100ns() / 10'000;
}

std::vector<OutputStatus> Streamer::Status()
{
    std::vector<OutputStatus> out;
    for (auto& o : m_outputs)
        out.push_back(o->Status());
    return out;
}

// نفس القطعة لكل المنصات (بدون نسخ: shared_ptr)
void Streamer::Broadcast(std::vector<EncodedPacket>& packets)
{
    for (EncodedPacket& p : packets)
    {
        auto shared = std::make_shared<const EncodedPacket>(std::move(p));
        for (auto& o : m_outputs)
            o->Push(shared);
    }
    packets.clear();
}

// =============================================================
//  خيط الصورة
// =============================================================
void Streamer::VideoThread()
{
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    std::vector<BYTE>    frame((size_t)m_width * m_height * 4);
    std::vector<uint8_t> nv12((size_t)m_width * m_height * 3 / 2);
    std::vector<EncodedPacket> packets;
    bool failed = false;

    {
        Compositor compositor(true);
        std::vector<SceneLayer> layers;
        int canvasW = 0, canvasH = 0;

        LONGLONG last = -1;
        timeBeginPeriod(1);
        while (m_running)
        {
            m_opt.scene->Snapshot(canvasW, canvasH, layers);
            compositor.Compose(layers, canvasW, canvasH, frame.data(), (int)m_width, (int)m_height, (int)m_width * 4, false);
            BgraToNv12(frame.data(), (int)m_width, (int)m_height, nv12.data());

            LONGLONG t = Elapsed100ns();
            if (t <= last) t = last + 1;
            last = t;

            const bool key = m_shared->forceKeyframe.exchange(false);
            if (FAILED(m_video.Encode(nv12.data(), t, key, packets)))
            {
                failed = true;
                break;
            }

            // أول مرة بيطلع SPS/PPS منحفظهم للمُرسِلات
            {
                std::lock_guard<std::mutex> lock(m_shared->mutex);
                if (m_shared->avcConfig.empty())
                    m_shared->avcConfig = m_video.AvcConfig();
            }
            Broadcast(packets);

            const LONGLONG next = (t / m_frameDuration + 1) * m_frameDuration;
            const LONGLONG now  = Elapsed100ns();
            if (next > now)
                Sleep((DWORD)((next - now) / 10'000));
        }
        timeEndPeriod(1);
    }

    if (SUCCEEDED(hrCom))
        CoUninitialize();

    if (failed)
    {
        m_running = false;
        PostMessageW(m_opt.notify, WM_STREAMER_DONE, (WPARAM)E_FAIL, 0);
    }
}

// =============================================================
//  خيط الصوت (حتى لو ما في مصادر منبعت سكوت - المنصات بتحب هيك)
// =============================================================
void Streamer::AudioThread()
{
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    LONGLONG written = 0;
    std::vector<float>   mix, tmp;
    std::vector<int16_t> pcm;
    std::vector<EncodedPacket> packets;

    while (m_running)
    {
        Sleep(10);

        const LONGLONG elapsedFrames = Elapsed100ns() * AudioSource::SAMPLE_RATE / ONE_SECOND;
        const LONGLONG target        = elapsedFrames - (LONGLONG)AUDIO_LAG_FRAMES;
        if (target - written < (LONGLONG)AAC_FRAME)
            continue;

        // منبعت مضاعفات 1024 (حجم إطار AAC)
        size_t frames = (size_t)std::min<LONGLONG>(target - written, AudioSource::SAMPLE_RATE);
        frames -= frames % AAC_FRAME;

        mix.assign(frames * AudioSource::CHANNELS, 0.0f);
        tmp.resize(frames * AudioSource::CHANNELS);
        for (size_t s = 0; s < m_opt.audio.size() && s < m_taps.size(); ++s)
        {
            AudioSource* src = m_opt.audio[s];
            const size_t got = src->Read(m_taps[s], tmp.data(), frames);
            for (size_t i = 0; i < got * AudioSource::CHANNELS; ++i)
                mix[i] += tmp[i];
            src->KeepNewest(m_taps[s], AUDIO_LAG_FRAMES * 3);
        }

        pcm.resize(mix.size());
        for (size_t i = 0; i < mix.size(); ++i)
        {
            const float v = std::clamp(mix[i], -1.0f, 1.0f);
            pcm[i] = (int16_t)(v * 32767.0f);
        }

        if (FAILED(m_audio.Encode(pcm.data(), frames, packets)))
            break;
        Broadcast(packets);
        written += frames;
    }

    if (SUCCEEDED(hrCom))
        CoUninitialize();
}
