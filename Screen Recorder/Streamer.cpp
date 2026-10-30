// =============================================================
//  Streamer.cpp - البث المباشر (+ الإعادة)
//
//   خيط الصورة (لكل نسخة): المشهد -> NV12 -> H.264 -> المنصات
//   خيط الصوت : خلط المصادر -> AAC -> كل المنصات
// =============================================================
#include "Streamer.h"
#include "Bitstream.h"
#include <algorithm>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace
{
    constexpr LONGLONG ONE_SECOND       = 10'000'000;
    constexpr size_t   AUDIO_LAG_FRAMES = AudioSource::SAMPLE_RATE / 10;   // 100ms
    constexpr size_t   AAC_FRAME        = 1024;

    // سجل تشخيص (stream_log.txt / replay_log.txt بمجلد إعدادات البرنامج)
    void WriteLog(const wchar_t* name, const std::wstring& text)
    {
        wchar_t dir[MAX_PATH];
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH))
            return;
        const std::wstring folder = std::wstring(dir) + L"\\ScreenRecorder";
        CreateDirectoryW(folder.c_str(), nullptr);
        HANDLE h = CreateFileW((folder + L"\\" + name).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return;
        const BYTE bom[] = { 0xFF, 0xFE };
        DWORD w = 0;
        WriteFile(h, bom, 2, &w, nullptr);
        WriteFile(h, text.data(), (DWORD)(text.size() * sizeof(wchar_t)), &w, nullptr);
        CloseHandle(h);
    }

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

    const UINT32 fps = std::clamp<UINT32>(m_opt.fps, 10, 60);
    m_opt.fps = fps;
    m_frameDuration = ONE_SECOND / fps;
    m_warning.clear();

    // ---- النسخ: كل (اتجاه + ترميز + معدّل بت) مختلف = نسخة. المتطابقين بيتشاركوا ----
    m_tracks.clear();
    auto trackFor = [&](bool vertical, bool hevc, UINT32 kbps) -> int
    {
        for (size_t i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i]->vertical == vertical && m_tracks[i]->hevc == hevc && m_tracks[i]->kbps == kbps)
                return (int)i;
        auto tr = std::make_unique<Track>();
        tr->active   = true;
        tr->vertical = vertical;
        tr->hevc     = hevc;
        tr->kbps     = kbps;
        tr->width    = std::max<UINT32>(64, (vertical ? m_opt.verticalW : m_opt.width) & ~1u);
        tr->height   = std::max<UINT32>(64, (vertical ? m_opt.verticalH : m_opt.height) & ~1u);
        m_tracks.push_back(std::move(tr));
        return (int)m_tracks.size() - 1;
    };
    if (m_opt.replay)
        trackFor(false, false, m_opt.videoKbps);           // الإعادة: النسخة رقم 0 دايماً
    std::vector<int> targetTrack;
    for (const StreamTarget& t : m_opt.targets)
    {
        const UINT32 kbps = t.kbps ? t.kbps : m_opt.videoKbps;
        const bool   hevc = t.codec == 1 && PlatformSupportsHevc(t.platform);
        targetTrack.push_back(trackFor(t.vertical, hevc, kbps));
    }

    // ---- المشفّرات: أول شي إنفيديا المباشر (أعلى جودة)، وإلا مشفّر ويندوز (H.264) ----
    const bool tryHw = m_opt.encoder != EncoderMode::Software;
    const bool live  = !m_opt.isReplay;
    m_encoderName.clear();
    std::wstring diag;
    {
        wchar_t buf[160];
        swprintf_s(buf, L"%s  %u إطار/ث  (%zu نسخة)\r\n", live ? L"البث" : L"الإعادة", fps, m_tracks.size());
        diag += buf;
    }
    auto fail = [&](const std::wstring& why)
    {
        for (auto& x : m_tracks) x->Reset();
        m_tracks.clear();
        error = why;
        WriteLog(live ? L"stream_log.txt" : L"replay_log.txt", diag);
    };
    for (size_t i = 0; i < m_tracks.size(); ++i)
    {
        Track& tr = *m_tracks[i];
        wchar_t head[160];
        swprintf_s(head, L"نسخة %zu: %s %ux%u  %s  %u كيلوبت/ث\r\n", i + 1, tr.vertical ? L"عمودية" : L"أفقية",
                   tr.width, tr.height, tr.hevc ? L"HEVC" : L"H.264", tr.kbps);
        diag += head;
        if (tryHw)
        {
            tr.comp = std::make_unique<Compositor>(true);
            if (tr.comp->EnsureGpu())
            {
                tr.pipe = std::make_unique<GpuFramePipe>();
                if (tr.pipe->Init(tr.comp->GpuDevice(), tr.width, tr.height, fps))
                {
                    tr.nv = std::make_unique<NvencEncoder>();
                    // البث: معدّل ثابت + B-frames + نظر لقدّام 8 إطارات (تأخير بسيط)
                    // الإعادة: معدّل متغيّر + 4 إطارات بلا B-frames (لحتى آخر لحظة تنحفظ لما تكبس)
                    if (tr.nv->Open(tr.comp->GpuDevice(), tr.hevc, tr.width, tr.height, fps, tr.kbps * 1000,
                                    live ? 2 : 0, diag, live, live ? 8 : 4))
                    {
                        const auto& seq = tr.nv->SequenceHeader();
                        tr.nvConfig = tr.hevc ? bs::BuildHvcC(seq.data(), seq.size()) : bs::BuildAvcC(seq.data(), seq.size());
                        tr.bframes = tr.nv->BFrames();
                        tr.videoOffset = tr.bframes * m_frameDuration;
                    }
                    if (tr.nvConfig.empty())
                        tr.nv.reset();
                }
            }
            if (!tr.nv)
            {
                tr.pipe.reset();
                tr.comp.reset();
            }
        }
        if (tr.nv)
        {
            if (m_encoderName.empty())
                m_encoderName = L"NVIDIA NVENC (مباشر)";
            continue;
        }
        // مشفّر ويندوز بيعمل H.264 بس
        if (tr.hevc)
        {
            tr.hevc = false;
            m_warning = L"HEVC يحتاج بطاقة NVIDIA، فاستُخدم H.264";
        }
        diag += L"   ← مشفّر ويندوز H.264 (احتياط)\r\n";
        if (FAILED(tr.encoder.Init(tr.width, tr.height, fps, tr.kbps, tryHw, true)))
        {
            fail(L"تعذّر تشغيل مشفّر الفيديو");
            return false;
        }
        if (m_encoderName.empty())
            m_encoderName = tr.encoder.Name();
    }
    WriteLog(live ? L"stream_log.txt" : L"replay_log.txt", diag);
    if (FAILED(m_audio.Init(m_opt.audioKbps)))
    {
        fail(L"تعذّر تشغيل مشفّر الصوت");
        return false;
    }

    // المُرسِلات: وحدة لكل منصة (بتاخد قطع النسخة تبعها)
    m_shared = std::make_shared<Shared>(m_tracks.size());
    std::shared_ptr<Shared> shared = m_shared;
    m_outputs.clear();
    m_outputTrack.clear();
    for (size_t i = 0; i < m_opt.targets.size(); ++i)
    {
        const int track = targetTrack[i];
        const Track& tr = *m_tracks[track];
        StreamInfo info;
        info.width = tr.width; info.height = tr.height; info.fps = fps;
        info.videoKbps = tr.kbps; info.audioKbps = m_opt.audioKbps;
        info.hevc = tr.hevc;
        m_outputs.push_back(std::make_unique<RtmpOutput>(
            m_opt.targets[i], info,
            [shared, track] { std::lock_guard<std::mutex> lock(shared->mutex); return shared->avcConfig[track]; },
            [shared, track] { shared->forceKeyframe[track] = true; }));
        m_outputTrack.push_back(track);
    }

    // نسخة على الجهاز: من النسخة الأفقية الأعلى جودة (بيفتح الملف أول ما يجي إطار كامل)
    m_file.reset();
    m_fileTried = false;
    m_fileTrack = -1;
    m_adaptivePercent = 100;
    if (!m_opt.saveCopyPath.empty() && live)
        for (size_t t = 0; t < m_tracks.size(); ++t)
            if (!m_tracks[t]->vertical && (m_fileTrack < 0 || m_tracks[t]->kbps > m_tracks[m_fileTrack]->kbps))
                m_fileTrack = (int)t;
    for (auto& tr : m_tracks)
        tr->curKbps = tr->kbps;

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
    for (size_t t = 0; t < m_tracks.size(); ++t)
        m_tracks[t]->thread = std::thread(&Streamer::VideoThread, this, (int)t);
    m_audioThread = std::thread(&Streamer::AudioThread, this);
    return true;
}

void Streamer::Stop()
{
    if (!m_active)
        return;
    m_running = false;
    for (auto& t : m_tracks)
        if (t->thread.joinable()) t->thread.join();
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

    // نسخة البث: منسكّرها بالخلفية (كتابة الفهرس بالآخر بتاخد لحظة) ومنخبّر الواجهة
    if (m_file)
    {
        std::shared_ptr<StreamFileWriter> file(std::move(m_file));
        const HWND notify = m_opt.notify;
        std::thread([file, notify]
        {
            const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const std::wstring path = file->Path();
            const bool ok = SUCCEEDED(file->Close());
            if (SUCCEEDED(hrCo)) CoUninitialize();
            auto* p = ok ? new std::wstring(path) : nullptr;
            if (!PostMessageW(notify, WM_STREAM_COPY_SAVED, 0, (LPARAM)p))
                delete p;
        }).detach();
    }

    // تسكير المشفّرات والتصوير بالخلفية: لحتى الواجهة ما تعلق أبداً لو التعريف تأخّر
    auto tracks = std::make_shared<std::vector<std::unique_ptr<Track>>>(std::move(m_tracks));
    m_tracks.clear();
    if (!tracks->empty())
        std::thread([tracks]
        {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (auto& t : *tracks) t->Reset();
            tracks->clear();
            if (SUCCEEDED(hr)) CoUninitialize();
        }).detach();
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

// =============================================================
//  البت ريت الذكي: إذا القطع عم تتجمّع عند المُرسِل (النت أبطأ من البث)
//  منخفّض المعدّل بخطوات، ولما يرجع النت منيح منرجّعه شوي شوي.
//  أحسن من إنو المنصة تقطّع أو نرمي إطارات.
// =============================================================
void Streamer::Adapt(int t)
{
    Track& tr = *m_tracks[t];
    static thread_local LONGLONG lastCheck = 0;
    const LONGLONG now = Elapsed100ns();
    if (now - lastCheck < ONE_SECOND)
        return;                                    // مرة بالثانية
    lastCheck = now;

    double backlog = 0;                            // كم ثانية عم تستنى عند أبطأ منصة
    for (size_t i = 0; i < m_outputs.size(); ++i)
        if (m_outputTrack[i] == t)
            backlog = std::max(backlog, m_outputs[i]->BufferedSeconds());

    UINT32 next = tr.curKbps;
    if (backlog > 0.8)                             // النت ضعف: نزول 25%
    {
        next = std::max<UINT32>(tr.kbps * 3 / 10, tr.curKbps * 3 / 4);
        tr.calmChecks = 0;
    }
    else if (backlog < 0.2 && tr.curKbps < tr.kbps)
    {
        if (++tr.calmChecks >= 8)                  // 8 ثواني هدوء: طلوع 10%
        {
            next = std::min<UINT32>(tr.kbps, tr.curKbps + std::max<UINT32>(250, tr.kbps / 10));
            tr.calmChecks = 0;
        }
    }
    else
        tr.calmChecks = 0;

    if (next != tr.curKbps && tr.nv->SetBitrate(next))
        tr.curKbps = next;

    int pct = 100;
    for (const auto& x : m_tracks)
        if (x->kbps) pct = std::min(pct, (int)(x->curKbps * 100 / x->kbps));
    m_adaptivePercent = pct;
}

// نفس القطعة لكل المنصات (بدون نسخ: shared_ptr)
void Streamer::Broadcast(std::vector<EncodedPacket>& packets)
{
    std::lock_guard<std::mutex> lock(m_broadcastMutex);
    for (EncodedPacket& p : packets)
    {
        // نسخة البث على الجهاز (نفس القطع بالضبط)
        if (m_fileTrack >= 0 && (!p.video || p.track == m_fileTrack))
        {
            if (!m_file && !m_fileTried && p.video && p.keyframe)
            {
                m_fileTried = true;
                std::vector<uint8_t> cfg;
                {
                    std::lock_guard<std::mutex> lk(m_shared->mutex);
                    cfg = m_shared->avcConfig[m_fileTrack];
                }
                const Track& tr = *m_tracks[m_fileTrack];
                auto file = std::make_unique<StreamFileWriter>();
                if (file->Open(m_opt.saveCopyPath, tr.hevc, tr.width, tr.height, m_opt.fps, tr.kbps, cfg,
                               tr.bframes > 0, m_opt.audioKbps))
                    m_file = std::move(file);
            }
            if (m_file)
                m_file->Write(p);
        }
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

    Track& track = *m_tracks[t];
    const UINT32 W = track.width, H = track.height;
    std::vector<BYTE>    frame((size_t)W * H * 4);
    std::vector<uint8_t> nv12((size_t)W * H * 3 / 2);
    std::vector<EncodedPacket> packets;
    bool failed = false;

    {
        std::unique_ptr<Compositor> ownCompositor;
        if (!track.comp)
            ownCompositor = std::make_unique<Compositor>(true);
        Compositor& compositor = track.comp ? *track.comp : *ownCompositor;
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
            if (track.vertical && canvasW > 0 && canvasH > 0)
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

            // الوقت على "الشبكة" بالضبط (0 ، 16.67 ، 33.33...) لحتى تطلع الحركة منتظمة
            LONGLONG now100 = Elapsed100ns() / m_frameDuration * m_frameDuration;
            if (last >= 0 && now100 <= last) now100 = last + m_frameDuration;
            last = now100;
            const bool key = m_shared->forceKeyframe[t].exchange(false);

            if (track.nv)
            {
                // إنفيديا المباشر: الصورة ما بتطلع من كرت الشاشة
                ID3D11Texture2D* tex = nullptr;
                cp.out = nullptr;
                cp.gpuOnly = true;
                cp.outTexture = &tex;
                compositor.Compose(layers, canvasW, canvasH, cp);
                if (!tex)
                {
                    failed = true;                       // كرت الشاشة وقف
                    break;
                }
                const int slot = track.pipe->Convert(tex);
                if (slot >= 0)
                {
                    if (m_opt.adaptive && !m_opt.isReplay)
                        Adapt(t);
                    std::vector<NvPacket> nvp;
                    const bool ok = track.nv->Encode(track.pipe->SlotTexture(slot), now100, m_frameDuration, nvp, key);
                    track.pipe->Release(slot);
                    if (!ok)
                    {
                        failed = true;
                        break;
                    }
                    for (NvPacket& np : nvp)
                    {
                        EncodedPacket pkt;
                        pkt.video    = true;
                        pkt.hevc     = track.hevc;
                        pkt.keyframe = bs::ToLengthPrefixed(np.data.data(), np.data.size(), track.hevc, pkt.data) || np.key;
                        // الوقت = وقت فك الضغط ، وفرق العرض (B-frames) لحالو
                        const LONGLONG dtsMs = (np.dts + track.videoOffset) / 10'000;
                        const LONGLONG ptsMs = (np.pts + track.videoOffset) / 10'000;
                        pkt.timeMs = (uint32_t)std::max<LONGLONG>(0, dtsMs);
                        pkt.ctsMs  = (int32_t)std::max<LONGLONG>(0, ptsMs - dtsMs);
                        if (!pkt.data.empty())
                            packets.push_back(std::move(pkt));
                    }
                }
                else if (key)
                    m_shared->forceKeyframe[t] = true;  // الإطار انترك: الإطار الكامل للمرة الجاية
            }
            else
            {
                compositor.Compose(layers, canvasW, canvasH, cp);
                BgraToNv12(frame.data(), (int)W, (int)H, nv12.data());
                if (FAILED(track.encoder.Encode(nv12.data(), now100, key, packets)))
                {
                    failed = true;
                    break;
                }
            }
            for (EncodedPacket& p : packets)
                p.track = (uint8_t)t;

            // أول مرة بيطلع SPS/PPS منحفظهم للمُرسِلات (وللإعادة)
            {
                std::lock_guard<std::mutex> lock(m_shared->mutex);
                if (m_shared->avcConfig[t].empty())
                {
                    m_shared->avcConfig[t] = track.nv ? track.nvConfig : track.encoder.AvcConfig();
                    if (t == 0 && m_opt.replay && !m_shared->avcConfig[t].empty() && !track.hevc)
                        m_opt.replay->SetVideoInfo(m_shared->avcConfig[t], (int)W, (int)H);
                }
            }
            Broadcast(packets);

            // منستنى لوقت الصورة الجاية بدقة (Sleep لحاله بيصحى بكّير شوي)
            const LONGLONG next = now100 + m_frameDuration;
            for (;;)
            {
                const LONGLONG now = Elapsed100ns();
                if (now >= next || !m_running) break;
                const LONGLONG ms = (next - now) / 10'000;
                Sleep(ms >= 2 ? (DWORD)(ms - 1) : (ms >= 1 ? 1 : 0));
            }
        }
        // إنفيديا: منفضّي المشفّر قبل ما ينسكّر (إذا انسكّر وفيه إطارات ناطرة، التعريف ممكن يعلق)
        if (track.nv)
        {
            std::vector<NvPacket> tail;
            track.nv->Flush(tail);
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
