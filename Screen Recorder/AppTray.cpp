// =============================================================
//  AppTray.cpp - أيقونة جنب الساعة: الإخفاء والإظهار، قائمتها، والخروج
// =============================================================
#include "App.h"

void ShowFromTray()
{
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

void HideToTray()
{
    g_panels.Close();
    ShowWindow(g_hwnd, SW_HIDE);
    if (!g_trayHintShown)       // أول مرة بس: منخبّره وين صار
    {
        g_tray.Balloon(L"مسجّل الشاشة ما زال يعمل",
                       L"البرنامج هنا بجانب الساعة. اضغط على أيقونته لفتحه، أو بالزر الأيمن للخروج.");
        g_trayHintShown = true;
        SaveSettings();
    }
}

void QuitApp()
{
    g_reallyQuit = true;
    DestroyWindow(g_hwnd);       // إذا عم يسجّل بينحفظ الملف أول (WM_DESTROY)
}

void ShowTrayMenu(POINT pt)
{
    const bool rec = g_recorder.IsRecording(), live = g_streamer.IsActive(), replay = g_replay.IsActive();
    std::vector<std::wstring> items;
    std::vector<int> codes;
    std::vector<MenuGlyph> glyphs;
    auto add = [&](const std::wstring& t, int c, const wchar_t* g = nullptr, bool danger = false)
    {
        items.push_back(t); codes.push_back(c); glyphs.push_back({ g, danger });
    };
    add(L"فتح مسجّل الشاشة", 0, L"\uE8A7");
    add(L"المعرض", 7, L"\uE8B9");
    add(L"الإعدادات", 6, Icon::Settings);
    add(L"", -1);
    add(rec ? L"إيقاف التسجيل" : L"بدء التسجيل", 1, rec ? L"\uE71A" : L"\uE7C8", rec);
    if (rec)
        add(g_recorder.IsPaused() ? L"استئناف التسجيل" : L"إيقاف مؤقت", 2, g_recorder.IsPaused() ? Icon::Play : Icon::Pause);
    add(live ? L"إيقاف البث" : L"بدء البث", 3, live ? L"\uE71A" : Icon::Live, live);
    if (replay)
        add(L"حفظ الإعادة", 4, L"\uE74E");
    add(replay ? L"إيقاف الإعادة" : L"تشغيل الإعادة", 5, replay ? L"\uE71A" : Icon::Replay, replay);
    add(L"", -1);
    add(L"خروج", 9, L"\uE7E8", true);

    SetForegroundWindow(g_hwnd);     // لازم لحتى تتسكّر القائمة لما تكبس برّاها
    const int pick = ShowMenu(items, -1, pt, {}, glyphs);
    if (pick < 0 || codes[pick] < 0)
        return;
    switch (codes[pick])
    {
    case 0: ShowFromTray(); break;
    case 1: ToggleRecording(); break;
    case 2: TogglePause(); break;
    case 3: ToggleStreaming(); break;
    case 4: SaveReplay(); break;
    case 5: if (replay) StopReplay(); else StartReplay(); break;
    case 6: ShowFromTray(); OpenSettings(g_settingsPage); break;
    case 7: ShowFromTray(); OpenGallery(); break;
    case 9: QuitApp(); break;
    }
}
