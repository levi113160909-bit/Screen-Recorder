// =============================================================
//  Streamer.cpp - البث المباشر (+ الإعادة)
//
//   خيط الصورة (لكل نسخة): المشهد -> NV12 -> H.264 -> المنصات
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
    if (m_opt.targets.empty() && !m_opt.replay)
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

    // أي نسخ منحتاج: الأفقية (للمنصات العادية والإعادة) والعمودية (تيك توك)
    bool needMain = m_opt.replay != nullptr, needVertical = false;
    for (const StreamTarget& t : m_opt.targets)
        (t.vertical ? needVertical : needMain) = true;

    const UINT32 fps = std::clamp<UINT32>(m_opt.fps, 10, 60);
    m_opt.fps = fps;
    m_frameDuration = ONE_SECOND / fps;

    m_tracks[0].active = needMain;
    m_tracks[0].width  = std::max<UINT32>(64, m_opt.width & ~1u);
    m_tracks[0].height = std::max<UINT32>(64, m_opt.height & ~1u);
    m_tracks[1].active = needVertical;
    m_tracks[1].width  = std::max<UINT32>(64, m_opt.verticalW & ~1u);
    m_tracks[1].height = std::max<UINT32>(64, m_opt.verticalH & ~1u);

    // المشفّرات
    const bool tryHw = m_opt.encoder != EncoderMode::Software;
    m_encoderName.clear();
    for (int t = 0; t < 2; ++t)
    {
        if (!m_tracks[t].active)
            continue;
        if (FAILED(m_tracks[t].encoder.Init(m_tracks[t].width, m_tracks[t].height, fps, m_opt.videoKbps, tryHw, true)))
        {
            for (Track& tr : m_tracks) tr.encoder.Shutdown();
            error = L"تعذّر تشغيل مشفّر الفيديو";
            return false;
        }
        if (m_encoderName.empty())
            m_encoderName = m_tracks[t].encoder.Name();
    }
    if (FAILED(m_audio.Init(m_opt.audioKbps)))
    {
        for (Track& tr : m_tracks) tr.encoder.Shutdown();
        error = L"تعذّر تشغيل مشفّر الصوت";
        return false;
    }

    // المُرسِلات: وحدة لكل منصة
    m_shared = std::make_shared<Shared>();
    std::shared_ptr<Shared> shared = m_shared;
    m_outputs.clear();
    m_outputTrack.clear();
    for (const StreamTarget& t : m_opt.targets)
    {
        const int track = t.vertical ? 1 : 0;
        StreamInfo info;
        info.width = m_tracks[track].width; info.height = m_tracks[track].height; info.fps = fps;
        info.videoKbps = m_opt.videoKbps; info.audioKbps = m_opt.audioKbps;
        m_outputs.push_back(std::make_unique<RtmpOutput>(
            t, info,
            [shared, track] { std::lock_guard<std::mutex> lock(shared->mutex); return shared->avcConfig[track]; },
            [shared, track] { shared->forceKeyframe[track] = true; }));
        m_outputTrack.push_back(track);
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
    for (int t = 0; t < 2; ++t)
        if (m_tracks[t].active)
            m_tracks[t].thread = std::thread(&Streamer::VideoThread, this, t);
    m_audioThread = std::thread(&Streamer::AudioThread, this);
    return true;
}

void Streamer::Stop()
{
    if (!m_active)
        return;
    m_running = false;
    for (Track& t : m_tracks)
        if (t.thread.joinable()) t.thread.join();
    if (m_audioThread.joinable()) m_audioThread.join();

    for (size_t i = 0; i < m_opt.audio.size() && i < m_taps.size(); ++i)
        m_opt.audio[i]->RemoveTap(m_taps[i]);
    m_taps.clear();

    // سكّرة الاتصالات ممكن تاخد شوي (نت بطيء) - منعملها بالخلفية
    // لحتى ما تعلق الواجهة
    auto outputs = std::make_shared<std::vector<std::unique_ptr<RtmpOutput>>>(std::move(m_outputs));
    m_outputs.clear();
    m_outputTrack.clear();
    if (!outputs->empty())
        std::thread([outputs] { outputs->clear(); }).detach();

    for (Track& t : m_tracks)
    {
        t.encoder.Shutdown();
        t.active = false;
    }
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
    std::lock_guard<std::mutex> lock(m_broadcastMutex);
    for (EncodedPacket& p : packets)
    {
        auto shared = std::make_shared<const EncodedPacket>(std::move(p));
        for (size_t i = 0; i < m_outputs.size(); ++i)
            if (!shared->video || shared->track == m_outputTrack[i])
                m_outputs[i]->Push(shared);
        if (m_opt.replay && (!shared->video || shared->track == 0))
            m_opt.replay->Push(shared);
    }
    packets.clear();
}

// =============================================================
//  خيط الصورة (وحدة لكل نسخة: أفقية / عمودية)
// =============================================================
void Streamer::VideoThread(int t)
{
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Track& track = m_tracks[t];
    const UINT32 W = track.width, H = track.height;
    std::vector<BYTE>    frame((size_t)W * H * 4);
    std::vector<uint8_t> nv12((size_t)W * H * 3 / 2);
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
            ComposeParams cp;
            cp.out = frame.data(); cp.outW = (int)W; cp.outH = (int)H; cp.stride = (int)W * 4;
            m_opt.scene->Snapshot(canvasW, canvasH, layers, cp.fade);

            // النسخة العمودية: أي جزء من اللوحة منعرض
            if (t == 1 && canvasW > 0 && canvasH > 0)
            {
                const double aspect = (double)W / H;                 // 9/16
                const double canvasAspect = (double)canvasW / canvasH;
                if (m_opt.verticalMode == VerticalMode::Crop)
                {
                    // قصّ من النص بطول اللوحة كلها
                    cp.viewH = canvasH;
                    cp.viewW = canvasH * aspect;
                    if (cp.viewW > canvasW) { cp.viewW = canvasW; cp.viewH = canvasW / aspect; }
                }
                else
                {
                    // اللوحة كلها بالعرض، وفوقها وتحتها أسود
                    cp.viewW = canvasW;
                    cp.viewH = canvasW / aspect;
                    if (canvasAspect < aspect) { cp.viewH = canvasH; cp.viewW = canvasH * aspect; }
                }
                cp.viewX = (canvasW - cp.viewW) / 2;
                cp.viewY = (canvasH - cp.viewH) / 2;
            }

            compositor.Compose(layers, canvasW, canvasH, cp);
            BgraToNv12(frame.data(), (int)W, (int)H, nv12.data());

            LONGLONG now100 = Elapsed100ns();
            if (now100 <= last) now100 = last + 1;
            last = now100;

            const bool key = m_shared->forceKeyframe[t].exchange(false);
            if (FAILED(track.encoder.Encode(nv12.data(), now100, key, packets)))
            {
                failed = true;
                break;
            }
            for (EncodedPacket& p : packets)
                p.track = (uint8_t)t;

            // أول مرة بيطلع SPS/PPS منحفظهم للمُرسِلات (وللإعادة)
            {
                std::lock_guard<std::mutex> lock(m_shared->mutex);
                if (m_shared->avcConfig[t].empty())
                {
                    m_shared->avcConfig[t] = track.encoder.AvcConfig();
                    if (t == 0 && m_opt.replay && !m_shared->avcConfig[t].empty())
                        m_opt.replay->SetVideoInfo(m_shared->avcConfig[t], (int)W, (int)H);
                }
            }
            Broadcast(packets);

            const LONGLONG next = (now100 / m_frameDuration + 1) * m_frameDuration;
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
        PostMessageW(m_opt.notify, WM_STREAMER_DONE, (WPARAM)E_FAIL, m_opt.isReplay ? 1 : 0);
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
