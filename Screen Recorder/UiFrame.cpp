// =============================================================
//  UiFrame.cpp - هيكل الواجهة: الشريط العلوي والجانبي والسفلي، والرسم الكامل لكل إطار
// =============================================================
#include "App.h"

// ---------- الشريط العلوي ----------
void DrawTopBar(float W)
{
    Circle(W - 36, 34, 14, Theme::Red);
    Circle(W - 36, 34, 5, Theme::White);
    Text(L"مسجّل الشاشة", g_fTitle.Get(), Rect(W - 330, 10, W - 60, 58), Theme::Text);

    // زر الإعدادات (ترس) جنب العنوان
    {
        const float tw = TextWidth(L"مسجّل الشاشة", g_fTitle.Get());
        const D2D1_RECT_F gear = Rect(W - 60 - tw - 140, 16, W - 60 - tw - 16, 52);
        const bool hover = AddHit(gear, ID_OPEN_SETTINGS);
        Fill(gear, hover ? Theme::Hover : Theme::Panel, 10);
        Stroke(gear, Theme::Border, 10);
        Text(Icon::Settings, g_fIcon.Get(), Rect(gear.right - 34, gear.top, gear.right - 10, gear.bottom), hover ? Theme::Text : Theme::SubText);
        Text(L"الإعدادات", g_fSegment.Get(), Rect(gear.left + 8, gear.top, gear.right - 36, gear.bottom), hover ? Theme::Text : Theme::SubText);
    }

    const bool rec    = g_recorder.IsRecording();
    const bool paused = g_recorder.IsPaused();
    D2D1_COLOR_F color = Theme::SubText;
    const wchar_t* label = L"جاهز";
    if (rec && paused) { color = Theme::Yellow; label = L"متوقف مؤقتاً"; }
    else if (rec)      { color = Theme::Red;    label = L"جارٍ التسجيل"; }

    const D2D1_RECT_F pill = Rect(16, 18, 146, 50);
    Fill(pill, WithAlpha(color, 0.15f), 16);
    if (!rec || paused || (g_tick / 15) % 2 == 0)
        Circle(pill.right - 18, 34, 5, color);
    Text(label, g_fCenter.Get(), Rect(pill.left + 6, pill.top, pill.right - 26, pill.bottom), color);

    Text(FormatTime(g_recorder.ElapsedMs()), g_fTimer.Get(), Rect(156, 10, 300, 58),
         rec ? Theme::Text : Theme::SubText);

    // شارة البث المباشر: "مباشر 00:12:34"
    if (g_streamer.IsActive())
    {
        const int  live  = LiveCount();
        const D2D1_COLOR_F c = live > 0 ? D2D1::ColorF(0xFF3B5C) : Theme::Yellow;
        double upKbps = 0;
        for (const OutputStatus& st : g_streamStatus) upKbps += st.kbps;
        wchar_t up[64];
        swprintf_s(up, L"الرفع %.1f ميغابت/ث", upKbps / 1000.0);
        const D2D1_RECT_F lp = Rect(312, 18, 652, 50);
        Fill(lp, WithAlpha(c, 0.16f), 16);
        if (live == 0 || (g_tick / 15) % 2 == 0)
            Circle(lp.right - 18, 34, 5, c);
        Text(live > 0 ? L"مباشر" : L"اتصال…", g_fSegment.Get(), Rect(lp.right - 80, lp.top, lp.right - 26, lp.bottom), c);
        Text(FormatTime(g_streamer.ElapsedMs()), g_fSmallLtr.Get(), Rect(lp.right - 170, lp.top, lp.right - 84, lp.bottom), Theme::Text);
        // الرفع اللحظي (مجموع كل المنصات)
        Fill(Rect(lp.right - 176, lp.top + 8, lp.right - 175, lp.bottom - 8), WithAlpha(c, 0.4f));
        Text(up, g_fSmall.Get(), Rect(lp.left + 12, lp.top, lp.right - 184, lp.bottom), live > 0 ? Theme::Green : Theme::SubText);
    }
}

// ---------- الشريط الجانبي ----------
void DrawSidebar(const D2D1_RECT_F& side)
{
    Fill(side, Theme::Panel, 14);
    DrawSegmented(Rect(side.left + 12, side.top + 12, side.right - 12, side.top + 46),
                  { L"المصادر", L"البث" }, g_tab, ID_TAB_SOURCES, true);
    if (g_tab == 1) DrawStreamTab(side, side.top + 58);
    else            DrawSourcesTab(side, side.top + 58);
}

// ---------- الشريط السفلي ----------
void DrawBottomBar(const D2D1_RECT_F& bar)
{
    Fill(bar, Theme::Panel, 14);
    const bool rec    = g_recorder.IsRecording();
    const bool paused = g_recorder.IsPaused();
    const float cy = (bar.top + bar.bottom) / 2;
    const float cx = (bar.left + bar.right) / 2;

    // زر التسجيل الكبير (بالنص)
    const float R = 26;
    const D2D1_RECT_F recRect = Rect(cx - R, cy - R, cx + R, cy + R);
    Circle(cx, cy, R, AddHit(recRect, ID_RECORD) ? Theme::RedHover : Theme::Red);
    Tip(ID_RECORD, rec ? L"إيقاف التسجيل" : L"بدء التسجيل");
    if (rec) Fill(Rect(cx - 9, cy - 9, cx + 9, cy + 9), Theme::White, 3);   // مربع = إيقاف
    else     Circle(cx, cy, 10, Theme::White);                               // دائرة = تسجيل

    // زر الإيقاف المؤقت
    const float pr = 20, px = cx - 66;
    const D2D1_RECT_F pauseRect = Rect(px - pr, cy - pr, px + pr, cy + pr);
    Circle(px, cy, pr, AddHit(pauseRect, ID_PAUSE) && rec ? Theme::Hover : Theme::Card);
    Tip(ID_PAUSE, !rec ? L"إيقاف مؤقت (أثناء التسجيل)" : (paused ? L"استئناف التسجيل" : L"إيقاف مؤقت"));
    Text(paused ? Icon::Play : Icon::Pause, g_fIcon.Get(), pauseRect,
         rec ? (paused ? Theme::Yellow : Theme::Text) : Theme::Off);

    // زر البث المباشر
    const bool streaming = g_streamer.IsActive();
    const D2D1_RECT_F live = Rect(cx - 246, cy - 20, cx - 98, cy + 20);
    const bool liveHover = AddHit(live, ID_STREAM);
    if (streaming) Fill(live, liveHover ? D2D1::ColorF(0xFF5C78) : D2D1::ColorF(0xFF3B5C), 20);
    else           Fill(live, liveHover ? Theme::Hover : Theme::Card, 20);
    Text(Icon::Live, g_fIcon.Get(), Rect(live.right - 40, live.top, live.right - 12, live.bottom),
         streaming ? Theme::White : D2D1::ColorF(0xFF3B5C));
    Text(streaming ? L"إيقاف البث" : L"بدء البث", g_fCenter.Get(), Rect(live.left + 8, live.top, live.right - 40, live.bottom),
         streaming ? Theme::White : Theme::Text);

    // زر المعرض (التسجيلات واللقطات والصور): أيقونة وتحتها كلمة "المعرض"
    const D2D1_RECT_F folder = Rect(bar.left + 12, cy - 27, bar.left + 70, cy + 27);
    const bool folderHover = AddHit(folder, ID_FOLDER);
    Fill(folder, folderHover ? Theme::Hover : Theme::Card, 10);
    Tip(ID_FOLDER, L"تسجيلاتك ولقطاتك وصورك");
    Text(L"\uE8B9", g_fIcon.Get(), Rect(folder.left, folder.top + 6, folder.right, folder.top + 30),
         folderHover ? Theme::Text : Theme::SubText);
    Text(L"المعرض", g_fSegment.Get(), Rect(folder.left, folder.top + 30, folder.right, folder.bottom - 4),
         folderHover ? Theme::Text : Theme::SubText);

    // زر الإعادة: مطفي = "تشغيل الإعادة" ، شغّال = "حفظ الإعادة" + زر صغير للإيقاف
    const bool replayOn = g_replay.IsActive();
    const D2D1_RECT_F rb = Rect(bar.left + 78, cy - 20, bar.left + 236, cy + 20);
    const bool rbHover = AddHit(rb, ID_REPLAY);
    if (replayOn) Fill(rb, rbHover ? WithAlpha(Theme::Accent, 0.32f) : WithAlpha(Theme::Accent, 0.18f), 10);
    else          Fill(rb, rbHover ? Theme::Hover : Theme::Card, 10);
    Text(Icon::Replay, g_fIcon.Get(), Rect(rb.right - 36, rb.top, rb.right - 8, rb.bottom), replayOn ? Theme::Accent : Theme::SubText);
    if (replayOn)
    {
        const D2D1_RECT_F stop = Rect(rb.left + 4, cy - 14, rb.left + 32, cy + 14);
        if (AddHit(stop, ID_REPLAY_STOP))
            Fill(stop, D2D1::ColorF(0xE5484D, 0.25f), 7);
        Tip(ID_REPLAY_STOP, L"إيقاف الإعادة");
        Text(Icon::Close, g_fIcon.Get(), stop, Theme::SubText);
        Text(L"حفظ الإعادة", g_fCenter.Get(), Rect(rb.left + 32, rb.top, rb.right - 36, rb.bottom), Theme::Text);
    }
    else
        Text(L"تشغيل الإعادة", g_fCenter.Get(), Rect(rb.left + 8, rb.top, rb.right - 36, rb.bottom), Theme::Text);

    // يمين: الرسالة + الاختصارات
    const D2D1_RECT_F info = Rect(cx + R + 24, bar.top, bar.right - 20, bar.bottom);
    if (!g_message.empty())
        Text(g_message, g_fBody.Get(), Rect(info.left, cy - 22, info.right, cy), g_messageIsError ? Theme::Red : Theme::Green);
    else if (streaming)
        Text(L"البث: " + std::to_wstring(LiveCount()) + L" من " + std::to_wstring(g_liveTargets.size()) + L" منصات على الهواء",
             g_fBody.Get(), Rect(info.left, cy - 22, info.right, cy), Theme::Text);
    else
        Text(rec ? L"المشفّر: " + g_recorder.EncoderName() : std::wstring(L"رتّب المشهد ثم اضغط الزر الأحمر"),
             g_fBody.Get(), Rect(info.left, cy - 22, info.right, cy), Theme::Text);
    {
        // الاختصارات الحالية (اللي إلها زر بس)
        std::wstring keys;
        const std::pair<int, const wchar_t*> shown[] = { { HK_RECORD, L"تسجيل" }, { HK_PAUSE, L"إيقاف مؤقت" },
                                                         { HK_STREAM, L"بث" }, { HK_REPLAY_SAVE, L"حفظ الإعادة" } };
        for (const auto& [action, label] : shown)
        {
            const std::wstring k = HotkeyManager::Text(g_hotkeys.Get(action));
            if (k.empty())
                continue;
            if (!keys.empty()) keys += L"  •  ";
            keys += LtrText(k) + L" " + label;
        }
        Text(keys, g_fSmall.Get(), Rect(info.left, cy + 2, info.right, cy + 22), Theme::SubText);
    }
}

void CreateRenderTarget()
{
    if (g_rt)
        return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = props.dpiY = (float)g_dpi;
    g_d2d->CreateHwndRenderTarget(props,
        D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)), &g_rt);
    if (g_rt)
    {
        g_rt->CreateSolidColorBrush(Theme::Text, &g_brush);
        g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }
}

void DiscardRenderTarget()
{
    g_logoRt.Reset();
    for (auto& b : g_logoBmp) b.Reset();
    g_previewBitmap.Reset();
    g_previewValid = false;
    g_brush.Reset();
    g_rt.Reset();
}

void Paint()
{
    CreateRenderTarget();
    if (!g_rt)
        return;

    g_hits.clear();
    BeginTooltips();
    g_rt->BeginDraw();
    g_rt->Clear(Theme::Background);

    const D2D1_SIZE_F size = g_rt->GetSize();
    const float W = size.width, H = size.height;
    const float sideW = 350;

    // إذا في نافذة داخلية مفتوحة: الواجهة اللي تحتها ما بتتفاعل مع الماوس
    const D2D1_POINT_2F realMouse = g_mouse;
    if (g_panels.IsOpen())
        g_mouse = D2D1::Point2F(-1, -1);

    DrawTopBar(W);
    const D2D1_RECT_F side = Rect(W - 16 - sideW, 68, W - 16, H - 104);
    DrawPreview(Rect(16, 68, side.left - 16, H - 104));
    DrawSidebar(side);
    DrawBottomBar(Rect(16, H - 90, W - 16, H - 16));

    g_mouse = realMouse;
    g_panels.Draw(W, H);     // النافذة الداخلية فوق كلشي
    DrawTooltip(W, H);       // التلميح فوق كلشي

    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET)
        DiscardRenderTarget();
}
