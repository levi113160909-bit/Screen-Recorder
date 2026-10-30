// =============================================================
//  ReplayControl.cpp - الإعادة: التسجيل المستمر بالذاكرة، الحفظ، وكشف الألعاب
// =============================================================
#include "App.h"

Scene                         g_replayScene;               // مشهد الإعادة لما المصدر "الشاشة كاملة"

bool                          g_systemForReplay = false;   // شغّلنا صوت النظام بس مشان الإعادة

// -------------------------------------------------------------
//  أماكن الحفظ
// -------------------------------------------------------------
// مجلد الإعادات: اللي اخترته، وإلا "Replays" جوّا مجلد التسجيلات
std::wstring ReplayFolder()
{
    return g_replaySet.folder.empty() ? g_folder + L"\\Replays" : g_replaySet.folder;
}

// الشاشات للإعادة (الرئيسية أول)
std::vector<CaptureTarget> ReplayMonitors()
{
    std::vector<CaptureTarget> mons;
    for (const CaptureTarget& t : ListCaptureTargets(g_hwnd))
        if (t.isMonitor)
            mons.push_back(t);
    std::stable_sort(mons.begin(), mons.end(),
                     [](const CaptureTarget& a, const CaptureTarget& b) { return a.isPrimary && !b.isPrimary; });
    return mons;
}

void StartReplay(bool quiet)
{
    if (g_replay.IsActive())
        return;

    StreamOptions opt;
    opt.notify   = g_hwnd;
    opt.isReplay = true;
    opt.replay   = g_replayBuffer;
    g_replayBuffer->Clear();
    g_replayBuffer->SetSeconds(REPLAY_SECONDS[g_replaySet.seconds]);

    // ---- الصورة: الشاشة كاملة (الافتراضي) أو المشهد الحالي ----
    int cw = 0, ch = 0;
    g_replayMonitorHandle = nullptr;
    // لعبة شغّالة: منلتقط نافذتها هي (أنضف: بلا إشعارات ويندوز، وبتضل حتى لو طلعت منها)
    if (g_replaySet.source == 0 && g_gameHwnd && IsWindow(g_gameHwnd))
    {
        int gw = 0, gh = 0;
        if (GetCaptureSize(g_gameTarget, gw, gh) && gw >= 64 && gh >= 64)
        {
            cw = gw & ~1;
            ch = gh & ~1;
            std::lock_guard<std::mutex> lock(g_replayScene.mutex);
            g_replayScene.canvasW = cw;
            g_replayScene.canvasH = ch;
            g_replayScene.layers.clear();
            SceneLayer l;
            l.id = 1;
            l.target = g_gameTarget;
            l.x = 0; l.y = 0; l.w = cw; l.h = ch;
            g_replayScene.layers.push_back(l);
            opt.scene = &g_replayScene;
            g_replayMonitorHandle = MonitorFromWindow(g_gameHwnd, MONITOR_DEFAULTTOPRIMARY);
        }
    }
    if (g_replaySet.source == 0 && !opt.scene)
    {
        const std::vector<CaptureTarget> mons = ReplayMonitors();
        if (!mons.empty())
        {
            const CaptureTarget& t = mons[std::clamp(g_replaySet.monitor, 0, (int)mons.size() - 1)];
            cw = (t.rect.right - t.rect.left) & ~1;
            ch = (t.rect.bottom - t.rect.top) & ~1;
            std::lock_guard<std::mutex> lock(g_replayScene.mutex);
            g_replayScene.canvasW = cw;
            g_replayScene.canvasH = ch;
            g_replayScene.layers.clear();
            SceneLayer l;
            l.id = 1;
            l.target = t;
            l.x = 0; l.y = 0; l.w = cw; l.h = ch;
            g_replayScene.layers.push_back(l);
            opt.scene = &g_replayScene;
            g_replayMonitorHandle = MonitorFromRect(&t.rect, MONITOR_DEFAULTTOPRIMARY);
        }
    }
    if (!opt.scene)                      // المشهد الحالي (أو ما لقينا شاشة)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        cw = g_scene.canvasW;
        ch = g_scene.canvasH;
        opt.scene = &g_scene;
    }

    // ---- الدقة والإطارات والجودة ----
    UINT32 h = g_replaySet.res == 1 ? 1080 : (g_replaySet.res == 2 ? 720 : (UINT32)ch);
    h = std::min<UINT32>(h, (UINT32)ch);
    UINT32 w = (UINT32)((double)cw * h / std::max(1, ch));
    opt.width     = std::max<UINT32>(16, w & ~1u);
    opt.height    = std::max<UINT32>(16, h & ~1u);
    opt.fps       = g_replaySet.fps == 0 ? 30 : 60;
    opt.videoKbps = ReplayKbps(g_replaySet, (int)opt.width, (int)opt.height);
    opt.encoder   = (EncoderMode)g_encoderMode;

    // ---- الصوت ----
    if (g_replaySet.system)
    {
        if (!g_systemAudio.IsRunning() && g_systemAudio.Start())
            g_systemForReplay = !g_systemOn;          // شغّلناه بس مشان الإعادة
        if (g_systemAudio.IsRunning())
            opt.audio.push_back(&g_systemAudio);
    }
    if (g_replaySet.mic && g_micOn && g_micAudio.IsRunning())
        opt.audio.push_back(&g_micAudio);
    if (g_replaySet.source == 1 && AnySceneHasApps())
        opt.audio.push_back(&g_appMixer.Output());

    std::wstring error;
    if (!g_replay.Start(opt, error))
    {
        ShowMessage(L"تعذّر تشغيل الإعادة: " + error, true);
        return;
    }
    g_replayWanted = true;
    SaveSettings();
    if (!quiet)
    {
        const std::wstring hint = KeyHint(HK_REPLAY_SAVE, L"لحفظ آخر " + std::to_wstring(REPLAY_SECONDS[g_replaySet.seconds]) + L" ثانية");
        ShowMessage(L"الإعادة تعمل" + (hint.empty() ? std::wstring() : L" — " + hint), false);
        Notify(Glyph::Replay, Theme::Accent, L"الإعادة تعمل", hint);
    }
}

void StopReplay()
{
    if (!g_replay.IsActive())
        return;
    g_replay.Stop();
    g_replayBuffer->Clear();
    g_replayWanted = false;
    if (g_systemForReplay && !g_systemOn)      // صوت النظام كان شغّال بس مشان الإعادة
        g_systemAudio.Stop();
    g_systemForReplay = false;
    SaveSettings();
    ShowMessage(L"أُوقفت الإعادة", false);
    Notify(Glyph::Replay, Theme::SubText, L"أُوقفت الإعادة");
}

// ---------- كشف الألعاب ----------
// إعادة تشغيل الإعادة بهدوء (مثلاً لتلتقط نافذة اللعبة بدل الشاشة)
void RestartReplayQuiet()
{
    if (!g_replay.IsActive())
        return;
    const bool wanted = g_replayWanted;
    g_replay.Stop();
    g_replayBuffer->Clear();
    StartReplay(true);
    g_replayWanted = wanted;
}

void StopReplayQuiet()
{
    if (!g_replay.IsActive())
        return;
    g_replay.Stop();
    g_replayBuffer->Clear();
    if (g_systemForReplay && !g_systemOn)
        g_systemAudio.Stop();
    g_systemForReplay = false;
}

void CheckGame(HWND hwnd)
{
    if (!g_autoGame)
        return;
    // اللعبة انسكّرت؟
    if (g_gameHwnd && !IsWindow(g_gameHwnd))
    {
        const std::wstring title = g_gameTarget.name;
        g_gameHwnd = nullptr;
        if (g_replayByGame)
        {
            StopReplayQuiet();
            Notify(Glyph::Game, Theme::SubText, L"أُغلقت اللعبة", L"أُوقفت الإعادة التلقائية");
        }
        else if (g_replay.IsActive() && g_replaySet.source == 0)
            RestartReplayQuiet();                    // رجوع لالتقاط الشاشة
        g_replayByGame = false;
        return;
    }
    if (g_gameHwnd || g_recorder.IsRecording())
        return;
    GameInfo gi;
    if (!DetectForegroundGame(hwnd, gi))
        return;
    g_gameHwnd = gi.hwnd;
    CaptureTarget t;
    t.isMonitor = false;
    t.hwnd      = gi.hwnd;
    t.name      = gi.title;
    t.exePath   = gi.exePath;
    wchar_t cls[256] = {};
    GetClassNameW(gi.hwnd, cls, 256);
    t.className = cls;
    GetWindowRect(gi.hwnd, &t.rect);
    g_gameTarget = t;

    if (!g_replay.IsActive())
    {
        const bool wanted = g_replayWanted;
        StartReplay(true);
        g_replayWanted = wanted;                     // التشغيل التلقائي ما بيغيّر اختيارك
        g_replayByGame = g_replay.IsActive();
    }
    else if (g_replaySet.source == 0)
        RestartReplayQuiet();

    const std::wstring key = LtrText(HotkeyManager::Text(g_hotkeys.Get(HK_REPLAY_SAVE)));
    Notify(Glyph::Game, Theme::Accent, L"اكتُشفت لعبة: " + gi.title,
           g_replay.IsActive() ? L"الإعادة تعمل • " + key + L" لحفظ آخر " + std::to_wstring(REPLAY_SECONDS[g_replaySet.seconds]) + L" ثانية"
                               : std::wstring());
}

void SaveReplay()
{
    if (!g_replay.IsActive())
    {
        ShowMessage(L"شغّل الإعادة أولاً (زر «الإعادة» في الأسفل)", true);
        return;
    }
    const std::wstring folder = ReplayFolder();
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, L"Replay_%04d-%02d-%02d_%02d-%02d-%02d.mp4",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring path = folder + L"\\" + name;

    std::wstring error;
    if (g_replayBuffer->Save(path, error))
    {
        g_lastFile = path;
        g_replaySavedTick = g_tick;          // المؤشر بيبيّن "حُفظ" ثانيتين
        ShowMessage(L"✓ حُفظت الإعادة: " + std::wstring(name), false);
        Notify(Glyph::Check, Theme::Green, L"حُفظت الإعادة", L"آخر " + std::to_wstring(REPLAY_SECONDS[g_replaySet.seconds]) + L" ثانية • " + name);
    }
    else
        ShowMessage(L"تعذّر حفظ الإعادة: " + error, true);
}

// إعدادات بتأثّر على الإعادة تغيّرت: منعيد تشغيلها
void RestartReplayIfActive()
{
    if (!g_replay.IsActive())
        return;
    g_replay.Stop();
    StartReplay(true);
}
