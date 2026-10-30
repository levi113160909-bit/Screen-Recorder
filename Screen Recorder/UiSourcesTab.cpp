// =============================================================
//  UiSourcesTab.cpp - تبويب المصادر: قائمة الطبقات وصفوف الصوت وأصوات البرامج
// =============================================================
#include "App.h"

// ---------- سطر مصدر صوت ----------
void DrawAudioRow(const D2D1_RECT_F& r, const wchar_t* icon, const wchar_t* title, AudioSource& src,
                  bool on, float level, int toggleId, int deviceId, int muteId, int volId, int index)
{
    const bool locked = Busy();
    const bool muted  = src.IsMuted();
    Fill(r, Theme::Card, 10);

    // الأيقونة = زر الكتم (بيشتغل حتى وقت التسجيل)
    const D2D1_RECT_F mute = Rect(r.right - 46, r.top + 6, r.right - 8, r.top + 40);
    if (AddHit(mute, muteId))
        Fill(mute, Theme::Hover, 8);
    Tip(muteId, muted ? L"إلغاء الكتم" : L"كتم");
    Text(muted ? Icon::Muted : icon, g_fIconBig.Get(), mute,
         muted ? Theme::Red : (on ? Theme::Text : Theme::SubText));
    // المايك: زر الفلاتر جنب زر الجهاز
    const float textLeft = index == 1 ? r.left + 130 : r.left + 96;
    if (index == 1)
    {
        const bool anyFilter = src.Filters().Any();
        const D2D1_RECT_F fb = Rect(r.left + 94, r.top + 9, r.left + 126, r.top + 41);
        Tip(ID_MIC_FILTERS, L"فلاتر الميكروفون (إزالة الضجيج وغيرها)");
        if (AddHit(fb, ID_MIC_FILTERS))
            Fill(fb, Theme::Hover, 8);
        else if (anyFilter)
            Fill(fb, WithAlpha(Theme::Accent, 0.16f), 8);
        Text(Icon::Filter, g_fIcon.Get(), fb, anyFilter ? Theme::Accent : Theme::SubText);
    }
    Text(title, g_fBody.Get(), Rect(textLeft, r.top + 6, r.right - 50, r.top + 26), Theme::Text);

    std::wstring sub;
    D2D1_COLOR_F subColor = Theme::SubText;
    if (!on)                    sub = L"معطّل";
    else if (!src.IsRunning()) { sub = L"لم يُعثر على جهاز"; subColor = Theme::Red; }
    else if (index == 1 && g_hotkeys.Get(HK_PUSH_TO_TALK).vk)      // اضغط لتتكلم
    {
        const std::wstring key = LtrText(HotkeyManager::Text(g_hotkeys.Get(HK_PUSH_TO_TALK)));
        sub      = muted ? L"اضغط لتتكلم: " + key : std::wstring(L"تتكلم الآن…");
        subColor = muted ? Theme::SubText : Theme::Green;
    }
    else if (muted)            { sub = L"مكتوم"; subColor = Theme::Red; }
    else                        sub = src.DeviceName();
    Text(sub, g_fSmall.Get(), Rect(textLeft, r.top + 26, r.right - 50, r.top + 44), subColor);

    // مفتاح التشغيل
    const D2D1_RECT_F toggle = Rect(r.left + 10, r.top + 12, r.left + 54, r.top + 38);
    AddHit(toggle, toggleId);
    Tip(toggleId, on ? L"تعطيل المصدر" : L"تفعيل المصدر");
    DrawToggle(r.left + 12, r.top + 13, on, !locked);

    // زر اختيار الجهاز
    const D2D1_RECT_F dev = Rect(r.left + 58, r.top + 9, r.left + 90, r.top + 41);
    if (AddHit(dev, deviceId) && !locked)
        Fill(dev, Theme::Hover, 8);
    Tip(deviceId, L"اختيار الجهاز");
    Text(Icon::Chevron, g_fIcon.Get(), dev, locked ? Theme::Off : Theme::SubText);

    // ---- شريط العلو + مؤشر الصوت جوّاته ----
    // من اليمين لليسار: 0% على اليمين و200% على اليسار
    const float cy = r.bottom - 20;
    const D2D1_RECT_F track = Rect(r.left + 58, cy - 3, r.right - 14, cy + 3);
    const float tw = track.right - track.left;
    g_volTrack[index] = track;
    AddHit(Rect(track.left - 8, cy - 12, track.right + 8, cy + 12), volId);

    const bool active = on && src.IsRunning() && !muted;
    Fill(track, Theme::Border, 3);
    if (active && level > 0)       // مستوى الصوت الحالي (بعد العلو)
    {
        const D2D1_COLOR_F c = level < 0.7f ? Theme::Green : (level < 0.9f ? Theme::Yellow : Theme::Red);
        Fill(Rect(track.right - tw * level, track.top, track.right, track.bottom), WithAlpha(c, 0.85f), 3);
    }

    // المقبض
    const float vol = src.Volume();
    const float kx = track.right - tw * (vol / 2.0f);
    const bool  dragging = (g_volDrag == &src);
    Circle(kx, cy, dragging ? 8.0f : 7.0f, muted ? Theme::Off : Theme::White);
    Circle(kx, cy, 3.0f, muted ? Theme::SubText : Theme::Accent);

    // النسبة
    Text(std::to_wstring((int)std::lround(vol * 100)) + L"%", g_fSmallLtr.Get(),
         Rect(r.left + 8, cy - 10, r.left + 54, cy + 10), muted ? Theme::Off : (dragging ? Theme::Accent : Theme::SubText));
}

// ---------- سطر صوت برنامج (بقائمة المصادر) ----------
void DrawAppAudioRow(const D2D1_RECT_F& row, int i)
{
    const AppAudioEntry e = g_appMixer.Entry(i);
    const bool connected  = g_appMixer.IsConnected(i);

    const D2D1_RECT_F del  = Rect(row.left + 6, row.top + 8, row.left + 36, row.top + 38);
    const D2D1_RECT_F mute = Rect(row.left + 40, row.top + 8, row.left + 70, row.top + 38);
    const bool hover     = AddHit(row, IDB_APP_ROW.Id(i));
    const bool delHover  = AddHit(del, IDB_APP_DELETE.Id(i));
    const bool muteHover = AddHit(mute, IDB_APP_MUTE.Id(i));
    Tip(IDB_APP_DELETE.Id(i), L"إزالة البرنامج من الخلّاط");
    Tip(IDB_APP_MUTE.Id(i), e.muted ? L"إلغاء الكتم" : L"كتم");

    Fill(row, hover ? Theme::Hover : Theme::Card, 10);
    Text(Icon::Speaker, g_fIconBig.Get(), Rect(row.right - 44, row.top + 2, row.right - 10, row.top + 40),
         connected ? Theme::Accent : Theme::SubText);
    Text(e.name, g_fBody.Get(), Rect(row.left + 76, row.top + 4, row.right - 50, row.top + 24),
         connected ? Theme::Text : Theme::SubText);

    std::wstring sub = L"صوت برنامج";
    D2D1_COLOR_F subColor = Theme::SubText;
    if (!connected)   { sub = L"البرنامج غير مفتوح"; subColor = Theme::Yellow; }
    else if (e.muted) { sub = L"مكتوم"; subColor = Theme::Red; }
    Text(sub, g_fSmall.Get(), Rect(row.left + 76, row.top + 23, row.right - 50, row.top + 41), subColor);

    if (delHover)  Fill(del, D2D1::ColorF(0xE5484D, 0.2f), 6);
    if (muteHover) Fill(mute, Theme::Hover, 6);
    Text(Icon::Delete, g_fIcon.Get(), del, delHover ? Theme::Red : Theme::SubText);
    Text(e.muted ? Icon::Muted : Icon::Speaker, g_fIcon.Get(), mute, e.muted ? Theme::Red : Theme::Text);

    // شريط العلو + مستوى الصوت جوّاته (من اليمين لليسار متل باقي الأشرطة)
    const float cy = row.bottom - 13;
    const D2D1_RECT_F track = Rect(row.left + 58, cy - 3, row.right - 14, cy + 3);
    const float tw = track.right - track.left;
    if (i < (int)g_appTracks.size())
        g_appTracks[i] = track;
    AddHit(Rect(track.left - 8, cy - 10, track.right + 8, cy + 10), IDB_APP_VOL.Id(i));

    Fill(track, Theme::Border, 3);
    const float level = i < (int)g_appLevels.size() ? g_appLevels[i] : 0.0f;
    if (connected && !e.muted && level > 0)
    {
        const D2D1_COLOR_F c = level < 0.7f ? Theme::Green : (level < 0.9f ? Theme::Yellow : Theme::Red);
        Fill(Rect(track.right - tw * level, track.top, track.right, track.bottom), WithAlpha(c, 0.85f), 3);
    }
    const bool  dragging = (g_appVolDrag == i);
    const float kx = track.right - tw * (e.volume / 2.0f);
    Circle(kx, cy, dragging ? 8.0f : 7.0f, e.muted ? Theme::Off : Theme::White);
    Circle(kx, cy, 3.0f, e.muted ? Theme::SubText : Theme::Accent);
    Text(std::to_wstring((int)std::lround(e.volume * 100)) + L"%", g_fSmallLtr.Get(),
         Rect(row.left + 8, cy - 10, row.left + 54, cy + 10), e.muted ? Theme::Off : (dragging ? Theme::Accent : Theme::SubText));
}

// ---------- تبويب المصادر: طبقات المشهد + الصوت ----------
void DrawSourcesTab(const D2D1_RECT_F& side, float y)
{
    // العنوان + زر الإضافة
    const std::wstring sceneName = (g_activeScene < (int)g_scenes.size()) ? g_scenes[g_activeScene].name : L"";
    Text(L"طبقات: " + sceneName, g_fSection.Get(), Rect(side.left + 140, y, side.right - 18, y + 30), Theme::SubText);
    const D2D1_RECT_F add = Rect(side.left + 12, y, side.left + 132, y + 30);
    Fill(add, AddHit(add, ID_ADD_SOURCE) ? D2D1::ColorF(0x6A9DFF) : Theme::Accent, 8);
    Text(L"+  إضافة مصدر", g_fSegment.Get(), add, Theme::White);

    const float audioHeight = 226;
    g_listRect = Rect(side.left + 8, y + 38, side.right - 8, side.bottom - audioHeight);

    std::vector<SceneLayer> layers;
    int cw, ch;
    g_scene.Snapshot(cw, ch, layers);

    const float rowH = 50, gap = 4;
    // أصوات البرامج بتنعرض بعد الطبقات: عنوان + سطر لكل برنامج (+ تنبيه إذا صوت النظام شغّال)
    const int   appCount = (int)g_appMixer.Count();
    const float appRowH  = 64;
    const bool  appWarn  = appCount > 0 && g_systemOn;
    const float appsH    = appCount == 0 ? 0 : 32 + appCount * (appRowH + gap) + (appWarn ? 40 : 0);
    const float contentH = layers.size() * (rowH + gap) + appsH;
    const float listH    = g_listRect.bottom - g_listRect.top;
    g_scrollMax = std::max(0.0f, contentH - listH);
    g_scroll = std::clamp(g_scroll, 0.0f, g_scrollMax);

    if (layers.empty() && appCount == 0)
        Text(L"لا توجد مصادر بعد", g_fCenter.Get(), g_listRect, Theme::SubText);

    g_rt->PushAxisAlignedClip(g_listRect, D2D1_ANTIALIAS_MODE_ALIASED);
    // الطبقة اللي فوق الكل منعرضها أول شي
    for (int k = 0; k < (int)layers.size(); ++k)
    {
        const SceneLayer& l = layers[layers.size() - 1 - k];
        const float top = g_listRect.top + k * (rowH + gap) - g_scroll;
        if (top + rowH < g_listRect.top || top > g_listRect.bottom)
            continue;
        const D2D1_RECT_F row = Rect(g_listRect.left, top, g_listRect.right, top + rowH);
        const D2D1_RECT_F vis = Rect(row.left, std::max(row.top, g_listRect.top), row.right, std::min(row.bottom, g_listRect.bottom));
        const bool selected = (l.id == g_selectedLayer);

        // زرّين على اليسار: حذف + إظهار/إخفاء (منسجّلهم قبل السطر لحتى ياخدوا الأولوية)
        const D2D1_RECT_F del = Rect(row.left + 6, row.top + 10, row.left + 36, row.top + 40);
        const D2D1_RECT_F eye = Rect(row.left + 40, row.top + 10, row.left + 70, row.top + 40);
        // طبقة رقمها برّا الكتلة (نادر جداً) بتنرسم بس ما بتنكبس، لحتى ما تنكبس غلط
        const bool clickable = IDB_LAYER.Fits(l.id);
        const bool hover    = clickable && AddHit(vis, IDB_LAYER.Id(l.id));
        const bool delHover = clickable && AddHit(del, IDB_LAYER_DELETE.Id(l.id));
        const bool eyeHover = clickable && AddHit(eye, IDB_LAYER_EYE.Id(l.id));
        Tip(IDB_LAYER_DELETE.Id(l.id), L"حذف المصدر");
        Tip(IDB_LAYER_EYE.Id(l.id), l.visible ? L"إخفاء المصدر" : L"إظهار المصدر");

        if (selected)
        {
            Fill(row, WithAlpha(Theme::Accent, 0.16f), 10);
            Stroke(row, WithAlpha(Theme::Accent, 0.7f), 10, 1);
        }
        else if (hover)
        {
            Fill(row, Theme::Hover, 10);
        }

        const D2D1_COLOR_F textColor = l.visible ? Theme::Text : Theme::SubText;
        const wchar_t* kindIcon = l.kind == LayerKind::Camera ? Icon::Camera : l.kind == LayerKind::Image ? Icon::Photo
                                : l.kind == LayerKind::Text ? Icon::Font : l.kind == LayerKind::Web ? Icon::Globe : (l.target.isMonitor ? Icon::Monitor : Icon::Window);
        Text(kindIcon, g_fIconBig.Get(),
             Rect(row.right - 44, row.top, row.right - 10, row.bottom), selected ? Theme::Accent : Theme::SubText);
        Text(l.target.name, g_fBody.Get(), Rect(row.left + 76, row.top + 5, row.right - 50, row.top + 27), textColor);

        std::wstring sub = l.kind == LayerKind::Camera ? L"كاميرا" : l.kind == LayerKind::Image ? L"صورة"
                         : l.kind == LayerKind::Text ? L"نص" : l.kind == LayerKind::Web ? L"صفحة ويب" : (l.target.isMonitor ? L"شاشة كاملة" : L"نافذة");
        D2D1_COLOR_F subColor = Theme::SubText;
        if (l.lost)
        {
            sub = l.kind == LayerKind::Camera ? L"الكاميرا غير متصلة" : l.kind == LayerKind::Image ? L"الملف غير موجود"
                : l.kind == LayerKind::Web ? (BrowserRuntimeAvailable() ? L"تعذّر فتح الصفحة • إعادة المحاولة تلقائيًا" : L"يلزم تثبيت WebView2") : L"النافذة مغلقة";
            subColor = Theme::Red;
        }
        else if (!l.visible) { sub += L" • مخفية"; }
        Text(sub, g_fSmall.Get(), Rect(row.left + 76, row.top + 26, row.right - 50, row.top + 44), subColor);

        if (delHover) Fill(del, D2D1::ColorF(0xE5484D, 0.2f), 6);
        if (eyeHover) Fill(eye, Theme::Hover, 6);
        Text(Icon::Delete, g_fIcon.Get(), del, delHover ? Theme::Red : Theme::SubText);
        Text(Icon::Eye, g_fIcon.Get(), eye, l.visible ? Theme::Text : Theme::Off);
    }

    // ---- أصوات البرامج ----
    g_appTracks.assign(appCount, D2D1_RECT_F{ -1, -1, -1, -1 });
    if (appCount > 0)
    {
        g_hitClipOn = true;               // اللي برّا القائمة (مخفي بالتمرير) ما بينكبس
        g_hitClip   = g_listRect;
        float ay = g_listRect.top + layers.size() * (rowH + gap) - g_scroll;
        Text(L"أصوات البرامج", g_fSection.Get(), Rect(g_listRect.left + 10, ay + 6, g_listRect.right - 10, ay + 28), Theme::SubText);
        ay += 32;
        for (int i = 0; i < appCount; ++i)
        {
            if (ay + appRowH >= g_listRect.top && ay <= g_listRect.bottom)
                DrawAppAudioRow(Rect(g_listRect.left, ay, g_listRect.right, ay + appRowH), i);
            ay += appRowH + gap;
        }
        if (appWarn)
        {
            Text(L"«صوت النظام» يلتقط هذه البرامج أيضًا،", g_fSmall.Get(),
                 Rect(g_listRect.left + 10, ay + 2, g_listRect.right - 10, ay + 20), Theme::Yellow);
            Text(L"فأطفئه لتجنّب تكرار الصوت", g_fSmall.Get(),
                 Rect(g_listRect.left + 10, ay + 20, g_listRect.right - 10, ay + 38), Theme::Yellow);
        }
        g_hitClipOn = false;
    }
    g_rt->PopAxisAlignedClip();

    // ---- قسم الصوت ----
    const float ay = side.bottom - audioHeight;
    Fill(Rect(side.left + 16, ay + 4, side.right - 16, ay + 5), Theme::Border);
    Text(L"الصوت", g_fSection.Get(), Rect(side.left + 18, ay + 10, side.right - 18, ay + 36), Theme::SubText);
    DrawAudioRow(Rect(side.left + 8, ay + 40, side.right - 8, ay + 124), Icon::Speaker, L"صوت النظام",
                 g_systemAudio, g_systemOn, g_systemLevel, ID_SYSTEM_AUDIO, ID_SYSTEM_DEVICE, ID_SYSTEM_MUTE, ID_SYSTEM_VOL, 0);
    DrawAudioRow(Rect(side.left + 8, ay + 130, side.right - 8, ay + 214), Icon::Mic, L"الميكروفون",
                 g_micAudio, g_micOn, g_micLevel, ID_MIC, ID_MIC_DEVICE, ID_MIC_MUTE, ID_MIC_VOL, 1);
}
