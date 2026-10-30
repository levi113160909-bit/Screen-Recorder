// =============================================================
//  UiStreamTab.cpp - تبويب البث: الوجهات، ملخّص الإعدادات، عدّاد الرفع، والكاميرا الافتراضية
// =============================================================
#include "App.h"

// ---------- الكاميرا الافتراضية ----------
void ToggleVirtualCam()
{
    const int st = VirtualCamStateCode();
    if (st == 2)
        VirtualCamStop();
    else if (st == 0)
    {
        if (!VirtualCamSupported())
        {
            ShowMessage(L"الكاميرا الافتراضية تتطلّب ويندوز 11", true);
            return;
        }
        ShowMessage(L"جارٍ تشغيل الكاميرا الافتراضية… (أول مرة يطلب ويندوز موافقتك لتثبيتها)", false);
        VirtualCamStart(g_hwnd, g_scene);
    }
}

// ---------- تبويب البث: الوجهات + إعدادات البث ----------
void DrawStreamTab(const D2D1_RECT_F& side, float y)
{
    const bool live = g_streamer.IsActive();
    const float L = side.left + 16, R = side.right - 16;

    Text(L"وجهات البث", g_fSection.Get(), Rect(side.left + 140, y, side.right - 18, y + 30), Theme::SubText);
    const D2D1_RECT_F add = Rect(side.left + 12, y, side.left + 132, y + 30);
    Fill(add, live ? Theme::Off : (AddHit(add, ID_ADD_TARGET) ? D2D1::ColorF(0x6A9DFF) : Theme::Accent), 8);
    Text(L"+  إضافة وجهة", g_fSegment.Get(), add, Theme::White);

    const float settingsH = 190;
    g_listRect = Rect(side.left + 8, y + 38, side.right - 8, side.bottom - settingsH);

    const float rowH = 62, gap = 6;
    const float contentH = g_targets.size() * (rowH + gap);
    const float listH    = g_listRect.bottom - g_listRect.top;
    g_scrollMax = std::max(0.0f, contentH - listH);
    g_scroll = std::clamp(g_scroll, 0.0f, g_scrollMax);

    if (g_targets.empty())
    {
        Text(L"لا توجد وجهات بعد", g_fCenter.Get(), Rect(g_listRect.left, g_listRect.top + 20, g_listRect.right, g_listRect.top + 50), Theme::SubText);
        Text(L"أضف يوتيوب أو تيك توك أو غيرها، ويمكنك البث إليها كلها معاً", g_fSmall.Get(),
             Rect(g_listRect.left + 8, g_listRect.top + 52, g_listRect.right - 8, g_listRect.top + 72), Theme::SubText);
    }

    g_rt->PushAxisAlignedClip(g_listRect, D2D1_ANTIALIAS_MODE_ALIASED);
    for (int i = 0; i < (int)g_targets.size(); ++i)
    {
        const StreamTarget& t = g_targets[i];
        const float top = g_listRect.top + i * (rowH + gap) - g_scroll;
        if (top + rowH < g_listRect.top || top > g_listRect.bottom)
            continue;
        const D2D1_RECT_F row = Rect(g_listRect.left, top, g_listRect.right, top + rowH);
        Fill(row, Theme::Card, 10);

        // شعار المنصة (مربع ملوّن)
        const D2D1_RECT_F badge = Rect(row.right - 50, row.top + 12, row.right - 12, row.top + 50);
        DrawPlatformIcon((int)t.platform, badge, t.enabled);

        Text(t.vertical ? t.name + L"  •  عمودي" : t.name, g_fBody.Get(), Rect(row.left + 96, row.top + 9, row.right - 60, row.top + 31),
             t.enabled ? Theme::Text : Theme::SubText);

        // السطر التاني: الحالة
        std::wstring sub;
        D2D1_COLOR_F subColor = Theme::SubText, dot = Theme::Off;
        const OutputStatus* st = TargetStatus(i);
        if (st)
        {
            switch (st->state)
            {
            case OutputState::Live:
            {
                wchar_t buf[96];
                swprintf_s(buf, L"مباشر • %d كيلوبت/ث", (int)std::lround(st->kbps));
                sub = buf;
                if (st->dropped > 0)
                    sub += L" • فُقد " + std::to_wstring(st->dropped) + L" إطار";
                subColor = Theme::Green; dot = Theme::Green;
                break;
            }
            case OutputState::Connecting:   sub = L"جارٍ الاتصال…"; subColor = dot = Theme::Yellow; break;
            case OutputState::Reconnecting: sub = L"إعادة الاتصال… " + st->message; subColor = dot = Theme::Yellow; break;
            case OutputState::Failed:       sub = st->message; subColor = dot = Theme::Red; break;
            default:                        sub = L"متوقف"; break;
            }
        }
        else if (!t.enabled)          sub = L"معطّلة";
        else if (t.url.empty())      { sub = L"أدخل عنوان الخادم من القائمة"; subColor = Theme::Yellow; }
        else if (t.key.empty())      { sub = L"أدخل مفتاح البث من القائمة"; subColor = Theme::Yellow; }
        else
            sub = L"جاهزة • " + std::wstring(TargetHevc(t) ? L"HEVC" : L"H.264") + L" • " +
                  std::to_wstring(TargetKbps(t)) + (t.kbps ? L"" : L" (عام)");
        if (st)
            Circle(row.right - 64, row.top + 42, 4, dot);
        Text(sub, g_fSmall.Get(), Rect(row.left + 96, row.top + 32, row.right - (st ? 74.0f : 60.0f), row.top + 52), subColor);

        // مفتاح التفعيل + زر القائمة
        const D2D1_RECT_F toggle = Rect(row.left + 10, row.top + 18, row.left + 54, row.top + 44);
        AddHit(toggle, IDB_TARGET_TOGGLE.Id(i));
        Tip(IDB_TARGET_TOGGLE.Id(i), t.enabled ? L"إيقاف البث على هذه المنصة" : L"تفعيل البث على هذه المنصة");
        DrawToggle(row.left + 12, row.top + 19, t.enabled, !live);

        const D2D1_RECT_F more = Rect(row.left + 58, row.top + 15, row.left + 90, row.top + 47);
        if (AddHit(more, IDB_TARGET_MENU.Id(i)) && !live)
            Fill(more, Theme::Hover, 8);
        Tip(IDB_TARGET_MENU.Id(i), L"خيارات المنصة");
        Text(Icon::More, g_fIcon.Get(), more, live ? Theme::Off : Theme::SubText);
    }
    g_rt->PopAxisAlignedClip();

    // ---- ملخّص البث (الإعدادات نفسها بنافذة الإعدادات ← البث) ----
    float sy = side.bottom - settingsH;
    Fill(Rect(side.left + 16, sy + 4, side.right - 16, sy + 5), Theme::Border);
    Text(L"البث", g_fSection.Get(), Rect(L + 150, sy + 12, R, sy + 34), Theme::SubText);
    const D2D1_RECT_F edit = Rect(L, sy + 10, L + 140, sy + 38);
    const bool editHover = AddHit(edit, ID_STREAM_SETTINGS);
    Fill(edit, editHover ? Theme::Hover : Theme::Card, 8);
    Text(Icon::Settings, g_fIcon.Get(), Rect(edit.right - 30, edit.top, edit.right - 6, edit.bottom), Theme::SubText);
    Text(L"إعدادات البث", g_fSegment.Get(), Rect(edit.left + 6, edit.top, edit.right - 30, edit.bottom), Theme::Text);

    UINT32 sw, sh;
    StreamSize(sw, sh);
    Text(SizeText((int)sw, (int)sh) + L"  •  " + std::to_wstring(StreamFps()) + L" إطار/ث  •  المعدّل العام " +
         std::to_wstring(g_streamKbps), g_fSmall.Get(), Rect(L, sy + 46, R, sy + 66), Theme::SubText);

    // عدّاد الرفع: الفعلي وقت البث ، والمتوقّع قبله
    std::wstring hint;
    if (live && !g_streamStatus.empty())
    {
        double now = 0;
        for (const OutputStatus& st : g_streamStatus) now += st.kbps;
        wchar_t buf[96];
        swprintf_s(buf, L"الرفع الآن: %.1f ميغابت/ث", now / 1000.0);
        hint = buf;
    }
    else
    {
        UINT32 total = 0; int encodes = 0;
        UploadEstimate(total, encodes);
        hint = encodes ? L"مجموع الرفع ≈ " + std::to_wstring((total + 999) / 1000) + L" ميغابت/ث" +
                         (encodes > 1 ? L" • " + std::to_wstring(encodes) + L" نسخ مضغوطة" : std::wstring())
                       : std::wstring(L"فعّل منصة واحدة على الأقل");
    }
    Text(hint, g_fSmall.Get(), Rect(L, sy + 68, R, sy + 88), live ? Theme::Green : Theme::SubText);
    if (live && g_streamer.AdaptivePercent() < 100)
        Text(L"الإنترنت ضعيف: خُفّضت الجودة مؤقتًا إلى " + std::to_wstring(g_streamer.AdaptivePercent()) + L"%",
             g_fSmall.Get(), Rect(L, sy + 90, R, sy + 110), Theme::Yellow);
    else if (live && g_streamer.SavingCopy())
        Text(L"تُحفظ نسخة من البث على جهازك", g_fSmall.Get(), Rect(L, sy + 90, R, sy + 110), Theme::SubText);
    if (!live)
        Text(L"إعدادات كل منصة من زر ⋯ بجانبها", g_fSmall.Get(), Rect(L, sy + 90, R, sy + 110), Theme::Off);

    // ---- الكاميرا الافتراضية: المشهد ككاميرا ويب (ديسكورد ، زوم ، تيمز...) ----
    {
        const float vy = sy + 124;
        Fill(Rect(side.left + 16, vy - 6, side.right - 16, vy - 5), Theme::Border);
        const int st = VirtualCamStateCode();
        const bool supported = VirtualCamSupported();
        const D2D1_RECT_F row = Rect(L - 6, vy, R + 6, vy + 56);
        if (AddHit(row, ID_VCAM_TOGGLE) && supported && (st == 0 || st == 2))
            Fill(row, Theme::Hover, 8);
        Text(Icon::Camera, g_fIcon.Get(), Rect(R - 28, vy + 6, R - 2, vy + 30), st == 2 ? Theme::Accent : Theme::SubText);
        Text(L"الكاميرا الافتراضية", g_fBody.Get(), Rect(L + 56, vy + 6, R - 34, vy + 28), Theme::Text);
        std::wstring sub;
        D2D1_COLOR_F subColor = Theme::SubText;
        if (!supported)       { sub = L"تتطلّب ويندوز 11"; subColor = Theme::Off; }
        else if (st == 1)     { sub = L"جارٍ التشغيل…"; subColor = Theme::Yellow; }
        else if (st == 3)     { sub = L"جارٍ الإيقاف…"; subColor = Theme::Yellow; }
        else if (st == 2 && VirtualCamInUse()) { sub = L"تعمل • يعرضها برنامج الآن"; subColor = Theme::Green; }
        else if (st == 2)     { sub = L"تعمل • اختر «مسجّل الشاشة» ككاميرا في ديسكورد أو زوم"; subColor = Theme::Green; }
        else                    sub = L"اعرض المشهد ككاميرا في ديسكورد وزوم وتيمز";
        Text(sub, g_fSmall.Get(), Rect(L + 56, vy + 30, R - 34, vy + 50), subColor);
        DrawToggle(L + 2, vy + 16, st == 1 || st == 2, supported && (st == 0 || st == 2));
    }
}
