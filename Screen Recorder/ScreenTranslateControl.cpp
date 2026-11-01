// =============================================================
//  ScreenTranslateControl.cpp - ترجمة الشاشة كلها بالبرنامج: الاختصار ، الطبقة المؤقتة بالبث ، الإشعارات
//  (الشغل نفسه بـ ScreenTranslate.cpp)
// =============================================================
#include "App.h"

namespace
{
    uint64_t g_notifiedJob = 0;

    RECT PrimaryMonitor()
    {
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
        return mi.rcMonitor;
    }

    // طبقة المصدر بالمشهد (الشاشة أو جهاز الالتقاط). لازم القفل ممسوك
    const SceneLayer* SourceLayer(const ScreenTranslateInfo& i)
    {
        for (const SceneLayer& l : g_scene.layers)
        {
            const bool match = i.fromScreen ? (l.kind == LayerKind::Capture && l.target.isMonitor && EqualRect(&l.target.rect, &i.monitor))
                                            : ((l.kind == LayerKind::Device || l.kind == LayerKind::Camera) && l.source == i.deviceLink);
            if (match && l.visible && l.w > 4 && l.h > 4)
                return &l;
        }
        return nullptr;
    }
}

bool ActiveTranslateProfile(TranslateProfile& out)
{
    std::wstring id;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (const SceneLayer& l : g_scene.layers)
            if (l.kind == LayerKind::Translation && !l.source.empty() && l.source != SCREEN_TRANSLATE_SOURCE)
            {
                id = l.source;
                break;
            }
    }
    if (!id.empty() && GetTranslateProfile(id, out))
        return true;
    const std::vector<TranslateProfile> all = TranslateProfiles();
    if (all.empty())
        return false;
    out = all.front();
    return true;
}

void ToggleScreenTranslate()
{
    const ScreenTranslateInfo info = GetScreenTranslateInfo();
    if (info.working || info.showing)
    {
        StopScreenTranslate();
        return;
    }
    TranslateProfile p;
    if (!ActiveTranslateProfile(p))
    {
        Notify(Glyph::Warning, Theme::Yellow, L"لا يوجد ملف ترجمة", L"افتح «ترجمة اللعبة» من «إضافة مصدر» واختر خدمة الترجمة أولًا");
        return;
    }
    RECT monitor{}, player{};
    if (p.fromScreen)
    {
        // الشاشة اللي عليها النافذة اللي قدامك (اللعبة)
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY), &mi);
        monitor = mi.rcMonitor;
        player  = monitor;
    }
    else
    {
        const RECT& m = p.overlayMonitor;
        player = (m.right - m.left >= 16 && m.bottom - m.top >= 16) ? m : PrimaryMonitor();
    }
    StartScreenTranslate(p, p.fromScreen, monitor, player);
    Notify(Glyph::Game, Theme::Accent, L"جارٍ ترجمة الشاشة…", L"اضغط الاختصار مرة أخرى للإخفاء");
}

void UpdateScreenTranslateLayer()
{
    UpdateScreenTranslate();
    const ScreenTranslateInfo info = GetScreenTranslateInfo();

    // إشعار مرة وحدة لما تخلص المحاولة
    if (info.finished && info.job != g_notifiedJob)
    {
        g_notifiedJob = info.job;
        if (info.showing)
            Notify(Glyph::Check, Theme::Green, L"تُرجمت الشاشة (" + LtrText(std::to_wstring(info.count)) + L" نص)",
                   info.error.empty() ? std::wstring(L"تختفي عند تغيّر الشاشة أو بالضغط مرة أخرى") : info.error);
        else if (!info.error.empty())
            Notify(Glyph::Warning, Theme::Yellow, L"تعذّرت ترجمة الشاشة", info.error);
    }

    // الطبقة المؤقتة بالبث: فوق طبقة المصدر بالظبط (نفس المكان والقص)
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    int index = -1;
    for (int i = 0; i < (int)g_scene.layers.size(); ++i)
        if (g_scene.layers[i].kind == LayerKind::Translation && g_scene.layers[i].source == SCREEN_TRANSLATE_SOURCE)
        {
            index = i;
            break;
        }
    const SceneLayer* src = info.streamLayer ? SourceLayer(info) : nullptr;
    if (!src)
    {
        if (index >= 0)
            g_scene.layers.erase(g_scene.layers.begin() + index);
        return;
    }
    const SceneLayer source = *src;
    if (index < 0)
    {
        SceneLayer l;
        l.kind               = LayerKind::Translation;
        l.source             = SCREEN_TRANSLATE_SOURCE;
        l.target.name        = L"ترجمة الشاشة";
        l.target.isMonitor   = false;
        l.id                 = g_scene.nextId++;
        g_scene.layers.push_back(l);                 // فوق كل شي
        index = (int)g_scene.layers.size() - 1;
    }
    SceneLayer& t = g_scene.layers[index];
    t.x = source.x; t.y = source.y; t.w = source.w; t.h = source.h;
    t.cropL = source.cropL; t.cropT = source.cropT; t.cropR = source.cropR; t.cropB = source.cropB;
    t.visible = true;
}
