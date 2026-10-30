// =============================================================
//  FirstRunSetup.cpp - الإعداد السريع بأول تشغيل (لقطات / تسجيل / بث)
// =============================================================
#include "App.h"

// ---------- الإعداد السريع (أول تشغيل) ----------
bool HasNvidiaEncoder()
{
    HMODULE m = LoadLibraryExW(L"nvEncodeAPI64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (m) FreeLibrary(m);
    return m != nullptr;
}

std::vector<std::wstring> SetupSummary(const SetupChoice& c)
{
    std::vector<std::wstring> out;
    int pw, ph;
    PrimaryMonitorSize(pw, ph);
    const bool nv = HasNvidiaEncoder();
    out.push_back(L"الشاشة: " + SizeText(pw, ph) + L" • " + std::to_wstring(MonitorHz()) + L" هرتز");
    out.push_back(nv ? std::wstring(L"بطاقة الرسومات: NVIDIA — سيُستخدم مشفّرها المباشر بأعلى جودة")
                     : std::wstring(L"سيُستخدم أفضل مشفّر متاح في جهازك"));
    if (c.clips)
        out.push_back(L"اللقطات: تعمل في الخلفية بجودة فائقة • " + LtrText(HotkeyManager::Text(g_hotkeys.Get(HK_REPLAY_SAVE))) +
                      L" لحفظ آخر " + std::to_wstring(REPLAY_SECONDS[c.clipSeconds]) + L" ثانية");
    if (c.record)
        out.push_back(L"التسجيل: " + LtrText(HotkeyManager::Text(g_hotkeys.Get(HK_RECORD))) + L" للبدء والإيقاف • 60 إطارًا • " +
                      (nv ? L"HEVC (أوضح بحجم أصغر)" : L"H.264"));
    if (c.stream)
        out.push_back(L"البث: 60 إطارًا • أضف منصتك (يوتيوب، تويتش…) من تبويب «البث»");
    out.push_back(L"لقطة شاشة: " + LtrText(HotkeyManager::Text(g_hotkeys.Get(HK_SCREENSHOT))));
    return out;
}

void ApplySetup(const SetupChoice& c)
{
    int pw, ph;
    PrimaryMonitorSize(pw, ph);
    const bool nv = HasNvidiaEncoder();
    g_encoderMode = 0;                                   // تلقائي
    if (c.record || c.stream)
    {
        g_canvasChoice = 0;                              // بحجم الشاشة
        ApplyCanvasChoice();
        g_fpsChoice = FPS_FIXED_FIRST;                   // 60
        g_quality   = 2;                                 // عالية
        g_codec     = nv ? 1 : 0;                        // HEVC مع إنفيديا
    }
    if (c.clips)
    {
        g_replaySet.source    = 0;
        g_replaySet.seconds   = c.clipSeconds;
        g_replaySet.quality   = 3;                       // فائقة (اللقطات قصيرة)
        g_replaySet.res       = ph > 1440 ? 1 : 0;       // الأصلية (و1080p للشاشات الأكبر من 2K)
        g_replaySet.fps       = 1;                       // 60
        g_replaySet.system    = true;
        g_replaySet.mic       = true;
        g_replaySet.autoStart = true;
        g_autoGame            = true;
    }
    if (c.stream)
    {
        g_streamRes        = (nv && ph >= 1440) ? 0 : 1;
        g_streamFpsChoice  = 2;                          // 60
        g_streamKbps       = 6000;                       // آمن لكل المنصات
        g_tab              = 1;                          // تبويب البث
    }
    g_firstRunDone = true;
    SaveSettings();
    if (c.clips)
    {
        if (g_replay.IsActive()) RestartReplayIfActive();
        else                     StartReplay(true);
    }
    Notify(Glyph::Check, Theme::Green, L"تم الإعداد!", c.stream ? L"أضف منصة البث من تبويب «البث»" : L"البرنامج جاهز");
}

void ShowFirstRun()
{
    FirstRunHooks h;
    h.saveKey = [] { return HotkeyManager::Text(g_hotkeys.Get(HK_REPLAY_SAVE)); };
    h.changeKey = [](std::function<void()> done)
    {
        g_hotkeys.Suspend(true);
        g_hotkeys.BeginCapture([done](const HotkeyBinding& b)
        {
            if (!(b.vk == VK_ESCAPE && b.mods == 0))
                g_hotkeys.Set(HK_REPLAY_SAVE, b);
            g_hotkeys.EndCapture();
            g_hotkeys.Suspend(false);
            SaveSettings();
            if (done) done();
        });
    };
    h.summary = [](const SetupChoice& c) { return SetupSummary(c); };
    h.apply   = [](const SetupChoice& c) { ApplySetup(c); };
    h.skip    = [] { g_firstRunDone = true; SaveSettings(); };
    g_panels.Open(std::make_unique<FirstRunPanel>(std::move(h)));
}
