// =============================================================
//  TranslationControl.cpp - ترجمة الألعاب بالمشهد: إضافة الطبقة ، مكانها ، النافذة ، والتأخير
// =============================================================
#include "App.h"

// أجهزة الالتقاط والكاميرات بالمشهد الحالي (مصادر للترجمة)
std::vector<CameraInfo> SceneDevices()
{
    std::vector<CameraInfo> list;
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    for (int pass = 0; pass < 2; ++pass)          // أجهزة الالتقاط أول ، بعدين الكاميرات
        for (const SceneLayer& l : g_scene.layers)
        {
            const bool want = pass == 0 ? l.kind == LayerKind::Device : l.kind == LayerKind::Camera;
            if (!want || l.source.empty())
                continue;
            bool dup = false;
            for (const CameraInfo& c : list) dup = dup || c.link == l.source;
            if (!dup)
                list.push_back({ l.target.name, l.source });
        }
    return list;
}

namespace
{
    std::vector<CaptureTarget> Monitors()
    {
        std::vector<CaptureTarget> out;
        for (const CaptureTarget& t : ListCaptureTargets(g_hwnd))
            if (t.isMonitor) out.push_back(t);
        std::stable_sort(out.begin(), out.end(), [](const CaptureTarget& a, const CaptureTarget& b) { return a.isPrimary && !b.isPrimary; });
        return out;
    }

    // مكان الترجمة (نسبة من صورة المصدر) -> مكانها بالإطار (حسب طبقة المصدر وقصّها). لازم القفل ممسوك
    bool MapRegion(const TranslateProfile& p, double& x, double& y, double& w, double& h)
    {
        for (const SceneLayer& l : g_scene.layers)
        {
            const bool match = p.fromScreen ? (l.kind == LayerKind::Capture && l.target.isMonitor && EqualRect(&l.target.rect, &p.monitor))
                                            : ((l.kind == LayerKind::Device || l.kind == LayerKind::Camera) && l.source == p.deviceLink);
            if (!match)
                continue;
            const double vw = std::max(0.05, 1 - l.cropL - l.cropR), vh = std::max(0.05, 1 - l.cropT - l.cropB);
            x = l.x + (p.rx - l.cropL) / vw * l.w;
            y = l.y + (p.ry - l.cropT) / vh * l.h;
            w = p.rw / vw * l.w;
            h = p.rh / vh * l.h;
            return w > 4 && h > 4;
        }
        return false;
    }

    void Place(SceneLayer& t, const TranslateProfile& p)
    {
        double x, y, w, h;
        if (!MapRegion(p, x, y, w, h))
        {
            // ما في طبقة للمصدر: تحت بالنص (مكان الترجمة المعتاد)
            const double cw = g_scene.canvasW, ch = g_scene.canvasH;
            w = cw * p.rw;
            h = ch * p.rh;
            x = cw * p.rx;
            y = ch * p.ry;
        }
        t.x = x; t.y = y; t.w = w; t.h = h;
    }
}

void AddTranslationLayer(const std::wstring& profileId)
{
    TranslateProfile p;
    if (!GetTranslateProfile(profileId, p))
        return;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        SceneLayer l;
        l.kind        = LayerKind::Translation;
        l.source      = p.id;
        l.target.name = L"ترجمة: " + p.name;
        l.target.isMonitor = false;
        l.id = g_scene.nextId++;
        Place(l, p);
        g_scene.layers.push_back(l);              // فوق كل شي
        g_selectedLayer = l.id;
    }
    SaveScenes();
    ShowMessage(L"أُضيفت ترجمة اللعبة • تظهر الترجمة العربية فوق مكان الإنكليزية", false);
}

// الطبقات اللي بتستعمل هالملف بترجع فوق مكان الترجمة
void FitTranslationLayers(const std::wstring& profileId)
{
    TranslateProfile p;
    if (!GetTranslateProfile(profileId, p))
        return;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (SceneLayer& l : g_scene.layers)
            if (l.kind == LayerKind::Translation && l.source == profileId)
            {
                Place(l, p);
                l.target.name = L"ترجمة: " + p.name;
            }
    }
    SaveScenes();
}

// layerId = 0: من "إضافة مصدر" (زر "إضافة إلى المشهد")
void OpenTranslationPanel(int layerId)
{
    std::wstring profileId;
    if (layerId)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(layerId);
        if (i < 0) return;
        profileId = g_scene.layers[i].source;
    }
    TranslationPanelHooks h;
    h.devices  = [] { return SceneDevices(); };
    h.monitors = [] { return Monitors(); };
    h.input    = [](const std::wstring& title, std::wstring& text, bool password, bool ltr, int limit)
    {
        return InputBox(title, text, password, ltr, limit);
    };
    h.menu     = [](const std::vector<std::wstring>& items, int checked)
    {
        POINT pt;
        GetCursorPos(&pt);
        return ShowMenu(items, checked, pt);
    };
    h.busy       = [] { return Busy() || g_streamer.IsActive(); };
    h.addToScene = [](const std::wstring& id) { AddTranslationLayer(id); };
    h.fitLayer   = [](const std::wstring& id) { FitTranslationLayers(id); };
    h.useProfile = [layerId](const std::wstring& id)
    {
        TranslateProfile p;
        GetTranslateProfile(id, p);
        {
            std::lock_guard<std::mutex> lock(g_scene.mutex);
            const int i = FindLayer(layerId);
            if (i < 0) return;
            g_scene.layers[i].source      = id;
            g_scene.layers[i].target.name = L"ترجمة: " + p.name;
        }
        SaveScenes();
    };
    g_panels.Open(std::make_unique<TranslationPanel>(std::move(h), profileId, layerId == 0));
}

// كل نص ثانية: إذا في ترجمة شغّالة بالمشهد منطبّق تأخيرها على الصورة والصوت
void UpdateMediaDelays()
{
    int delay = 0;
    bool all = false;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (const SceneLayer& l : g_scene.layers)
        {
            if (l.kind != LayerKind::Translation || !l.visible)
                continue;
            TranslateProfile p;
            if (GetTranslateProfile(l.source, p) && !p.fromScreen)
            {
                delay = std::max(delay, p.delayMs);
                all = all || p.delayAll;
            }
        }
    }
    static int lastDelay = -1;
    static bool lastAll = false;
    if (delay == lastDelay && all == lastAll)
        return;
    lastDelay = delay;
    lastAll = all;
    const int others = all ? delay : 0;
    SetMediaDelays(delay, others);
    g_micAudio.SetDelayMs(others);
    g_systemAudio.SetDelayMs(others);
    g_appMixer.SetExtraDelays(delay, others);
}

// الاختصار: إظهار الترجمة فوق اللعبة على شاشتك أو إخفاؤها
//  - في ملفات مفعّلة: منطفّيها كلها (ومنتذكّرها)
//  - ما في: منرجّع اللي طفّيناها ، وإلا ملف طبقة الترجمة بالمشهد ، وإلا أول ملف
void ToggleTranslationOverlay()
{
    static std::vector<std::wstring> lastOn;
    std::vector<TranslateProfile> all = TranslateProfiles();
    if (all.empty())
    {
        Notify(Glyph::Warning, Theme::Yellow, L"لا يوجد ملف ترجمة", L"افتح «ترجمة اللعبة» من «إضافة مصدر» أولًا");
        return;
    }
    std::vector<std::wstring> on;
    for (const TranslateProfile& p : all)
        if (p.playerOverlay) on.push_back(p.id);
    if (!on.empty())
    {
        lastOn = on;
        for (TranslateProfile& p : all)
            if (p.playerOverlay) { p.playerOverlay = false; SetTranslateProfile(p, true); }
        Notify(Glyph::Check, Theme::SubText, L"أُخفيت الترجمة عن شاشتك", L"تبقى ظاهرة للمشاهدين في البث");
        return;
    }
    std::vector<std::wstring> want;
    for (const std::wstring& id : lastOn)
        for (const TranslateProfile& p : all)
            if (p.id == id) want.push_back(id);
    if (want.empty())
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (const SceneLayer& l : g_scene.layers)
            if (l.kind == LayerKind::Translation && !l.source.empty()) { want.push_back(l.source); break; }
    }
    if (want.empty())
        want.push_back(all.front().id);
    std::wstring name;
    for (TranslateProfile& p : all)
        if (std::find(want.begin(), want.end(), p.id) != want.end())
        {
            p.playerOverlay = true;
            SetTranslateProfile(p, true);
            if (name.empty()) name = p.name;
        }
    Notify(Glyph::Game, Theme::Green, L"الترجمة تظهر على شاشتك", L"«" + name + L"» • لا تظهر في البث");
}

// العكس تبع MapRegion: طبقة الترجمة بالمشهد -> نسبة من صورة المصدر
// (فالترجمة على شاشتك بتطلع بنفس مكان وحجم اللي بيشوفه الجمهور)
bool TranslationLayerRegion(const TranslateProfile& p, double& rx, double& ry, double& rw, double& rh)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const SceneLayer* t = nullptr;
    for (const SceneLayer& l : g_scene.layers)
        if (l.kind == LayerKind::Translation && l.source == p.id && l.visible) { t = &l; break; }
    if (!t || t->w < 4 || t->h < 4)
        return false;
    for (const SceneLayer& l : g_scene.layers)
    {
        const bool match = p.fromScreen ? (l.kind == LayerKind::Capture && l.target.isMonitor && EqualRect(&l.target.rect, &p.monitor))
                                        : ((l.kind == LayerKind::Device || l.kind == LayerKind::Camera) && l.source == p.deviceLink);
        if (!match || l.w < 4 || l.h < 4)
            continue;
        const double vw = std::max(0.05, 1 - l.cropL - l.cropR), vh = std::max(0.05, 1 - l.cropT - l.cropB);
        rx = l.cropL + (t->x - l.x) / l.w * vw;
        ry = l.cropT + (t->y - l.y) / l.h * vh;
        rw = t->w / l.w * vw;
        rh = t->h / l.h * vh;
        return rw > 0.005 && rh > 0.005;
    }
    // ما في طبقة للمصدر: الطبقة كنسبة من الإطار (متل Place)
    if (g_scene.canvasW < 16 || g_scene.canvasH < 16)
        return false;
    rx = t->x / g_scene.canvasW;
    ry = t->y / g_scene.canvasH;
    rw = t->w / g_scene.canvasW;
    rh = t->h / g_scene.canvasH;
    return true;
}
