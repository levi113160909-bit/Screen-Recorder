// =============================================================
//  Scenes.cpp - المشاهد: إضافة، تبديل، تسمية، نسخ، حذف، وحفظ وتحميل من الملف
// =============================================================
#include "App.h"

std::wstring ScenesPath()
{
    PWSTR appData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
    {
        path = std::wstring(appData) + L"\\ScreenRecorder";
        CoTaskMemFree(appData);
        CreateDirectoryW(path.c_str(), nullptr);
        path += L"\\scenes.txt";
    }
    return path;
}

// منرجّع طبقات المشهد الشغّال لمكانها بالقائمة
void StoreActiveScene()
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    if (g_activeScene >= 0 && g_activeScene < (int)g_scenes.size())
    {
        g_scenes[g_activeScene].layers = g_scene.layers;
        g_scenes[g_activeScene].apps   = g_appMixer.Entries();
    }
}

// الشاشة اللي عم تتسجّل (لمؤشر التسجيل): أكبر شاشة أو نافذة ظاهرة بالمشهد
// (الشاشة نفسها، أو الشاشة اللي عليها النافذة). كل ثانية منحسبها من جديد
HMONITOR RecordedMonitor()
{
    static HMONITOR cached = nullptr;
    static int      lastTick = -100;
    if (g_tick - lastTick < 30)
        return cached;
    lastTick = g_tick;
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    double best = 0;
    HMONITOR found = nullptr;
    for (const SceneLayer& l : g_scene.layers)
    {
        if (l.kind != LayerKind::Capture || !l.visible || l.lost || l.w * l.h <= best)
            continue;
        HMONITOR m = nullptr;
        if (l.target.isMonitor)
        {
            const POINT c{ (l.target.rect.left + l.target.rect.right) / 2, (l.target.rect.top + l.target.rect.bottom) / 2 };
            m = MonitorFromPoint(c, MONITOR_DEFAULTTONULL);
        }
        else if (l.target.hwnd && IsWindow(l.target.hwnd))
            m = MonitorFromWindow(l.target.hwnd, MONITOR_DEFAULTTONULL);
        if (m)
        {
            best  = l.w * l.h;
            found = m;
        }
    }
    cached = found;          // فاضي = الرئيسية
    return cached;
}

// اختصارات المشاهد: كل ما تغيّرت المشاهد منبعت اللائحة لمدير الاختصارات
void SyncSceneHotkeys()
{
    std::vector<std::wstring>  names;
    std::vector<HotkeyBinding> keys;
    for (const SceneData& s : g_scenes)
    {
        names.push_back(s.name);
        keys.push_back(s.hotkey);
    }
    g_hotkeys.SetScenes(names, keys);
    if (g_hwnd)
        g_hotkeys.Apply(g_hwnd);
}

// والعكس: بعد ما تغيّر اختصار بالنافذة منرجّعه للمشاهد (لحتى ينحفظ معها)
void SceneHotkeysFromManager()
{
    for (int i = 0; i < (int)g_scenes.size(); ++i)
        g_scenes[i].hotkey = g_hotkeys.Get(HK_COUNT + i);
}

// في ولا مشهد فيه أصوات برامج؟ (وقتها منضيف الخلّاط للتسجيل والبث)
bool AnySceneHasApps()
{
    if (g_appMixer.Count() > 0)
        return true;
    for (const SceneData& s : g_scenes)
        if (!s.apps.empty())
            return true;
    return false;
}

void SwitchScene(int index)
{
    if (index < 0 || index >= (int)g_scenes.size() || index == g_activeScene)
        return;
    StoreActiveScene();
    g_appMixer.SetEntries(g_scenes[index].apps);    // أصوات برامج المشهد الجديد
    g_appLevels.clear();
    // التسجيل بيكمّل بالمشهد الجديد فوراً (مع تلاشي ناعم إذا مفعّل)
    g_scene.ReplaceLayers(g_scenes[index].layers, TRANSITION_MS[g_transitionChoice]);
    if (TRANSITION_MS[g_transitionChoice] > 0)
        g_fadeTicks = TRANSITION_MS[g_transitionChoice] / 33 + 2;
    g_activeScene   = index;
    g_selectedLayer = 0;
    g_scroll        = 0;
    Notify(Glyph::Scene, Theme::Accent, L"المشهد: " + g_scenes[index].name);
}

std::wstring NewSceneName()
{
    for (int n = 1;; ++n)
    {
        const std::wstring name = L"المشهد " + std::to_wstring(n);
        bool used = false;
        for (const SceneData& s : g_scenes)
            if (s.name == name) { used = true; break; }
        if (!used)
            return name;
    }
}

void AddScene()
{
    StoreActiveScene();
    g_scenes.push_back({ NewSceneName(), {} });
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        g_scene.layers.clear();
    }
    g_appMixer.SetEntries({});
    g_appLevels.clear();
    SyncSceneHotkeys();
    g_activeScene   = (int)g_scenes.size() - 1;
    g_selectedLayer = 0;
}

void DuplicateScene(int index)
{
    StoreActiveScene();
    SceneData copy = g_scenes[index];
    copy.name += L" (نسخة)";
    copy.hotkey = {};                             // الاختصار ما بيتكرر
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (SceneLayer& l : copy.layers)
            l.id = g_scene.nextId++;              // أرقام جديدة للطبقات
    }
    g_scenes.insert(g_scenes.begin() + index + 1, copy);
    if (g_activeScene > index)
        ++g_activeScene;
    SyncSceneHotkeys();
    SwitchScene(index + 1);
}

// اسم تلقائي؟ ("المشهد 3") — الأسماء اللي اخترتها انت ما منلمسها
bool IsAutoSceneName(const std::wstring& name)
{
    const std::wstring prefix = L"المشهد ";
    if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
        return false;
    for (size_t i = prefix.size(); i < name.size(); ++i)
        if (name[i] < L'0' || name[i] > L'9') return false;
    return true;
}

// بعد الحذف: المشاهد بأسماء تلقائية بتاخد رقم مكانها (1 ، 2 ، 3...) لحتى يضل الترقيم متسلسل
void RenumberScenes()
{
    for (size_t i = 0; i < g_scenes.size(); ++i)
        if (IsAutoSceneName(g_scenes[i].name))
        {
            const std::wstring want = L"المشهد " + std::to_wstring(i + 1);
            // ما منعمل اسمين متشابهين (إذا مشهد تاني باسم خاص ماخد نفس الاسم)
            bool taken = false;
            for (size_t j = 0; j < g_scenes.size(); ++j)
                if (j != i && !IsAutoSceneName(g_scenes[j].name) && g_scenes[j].name == want) taken = true;
            if (!taken)
                g_scenes[i].name = want;
        }
}

void DeleteScene(int index)
{
    if (g_scenes.size() <= 1 || index < 0 || index >= (int)g_scenes.size())
        return;
    StoreActiveScene();
    const bool deletingActive = (index == g_activeScene);
    g_scenes.erase(g_scenes.begin() + index);
    RenumberScenes();
    SyncSceneHotkeys();
    if (index < g_activeScene)
        --g_activeScene;
    g_activeScene = std::clamp(g_activeScene, 0, (int)g_scenes.size() - 1);
    if (deletingActive)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        g_scene.layers = g_scenes[g_activeScene].layers;
        g_selectedLayer = 0;
        g_appMixer.SetEntries(g_scenes[g_activeScene].apps);
        g_appLevels.clear();
    }
}

// ---------- الحفظ بملف (لحتى ترجع المشاهد لما تفتح البرنامج) ----------
std::wstring Clean(std::wstring s)       // ممنوع tab وسطر جديد جوّا القيم
{
    for (wchar_t& c : s)
        if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
    return s;
}

void SaveScenes()
{
    StoreActiveScene();
    const std::wstring path = ScenesPath();
    FILE* f = nullptr;
    if (path.empty() || _wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f)
        return;
    fwprintf(f, L"A\t%d\n", g_activeScene);
    for (const SceneData& s : g_scenes)
    {
        fwprintf(f, L"S\t%ls\t%u\t%u\n", Clean(s.name).c_str(), s.hotkey.mods, s.hotkey.vk);
        for (const SceneLayer& l : s.layers)
        {
            if (l.kind != LayerKind::Capture)       // كاميرا / صورة / نص
            {
                fwprintf(f, L"M\t%d\t%.2f\t%.2f\t%.2f\t%.2f\t%d\t%d\t%d\t%u\t%d\t%ls\t%ls\t%.5f\t%.5f\t%.5f\t%.5f\t%d\t%u\t%.3f\t%.3f\t%.3f\t%d\t%d\n",
                         (int)l.kind, l.x, l.y, l.w, l.h, l.visible ? 1 : 0, l.shape, l.mirror ? 1 : 0,
                         l.color, l.background ? 1 : 0, Clean(l.source).c_str(), Clean(l.target.name).c_str(),
                         l.cropL, l.cropT, l.cropR, l.cropB,
                         l.chroma ? 1 : 0, l.chromaColor, l.chromaSim, l.chromaSmooth, l.chromaSpill, l.srcW, l.srcH);
                continue;
            }
            const RECT& r = l.target.rect;
            fwprintf(f, L"L\t%lc\t%ld\t%ld\t%ld\t%ld\t%.2f\t%.2f\t%.2f\t%.2f\t%d\t%ls\t%ls\t%ls\t%.5f\t%.5f\t%.5f\t%.5f\n",
                     l.target.isMonitor ? L'm' : L'w', r.left, r.top, r.right, r.bottom,
                     l.x, l.y, l.w, l.h, l.visible ? 1 : 0,
                     Clean(l.target.className).c_str(), Clean(l.target.exePath).c_str(), Clean(l.target.name).c_str(),
                     l.cropL, l.cropT, l.cropR, l.cropB);
        }
        for (const AppAudioEntry& a : s.apps)       // أصوات البرامج
            fwprintf(f, L"P\t%d\t%d\t%ls\t%ls\n", (int)std::lround(a.volume * 100), a.muted ? 1 : 0,
                     Clean(a.exePath).c_str(), Clean(a.name).c_str());
    }
    fclose(f);
}

std::vector<std::wstring> SplitTabs(const std::wstring& line)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;)
    {
        const size_t tab = line.find(L'\t', start);
        parts.push_back(line.substr(start, tab == std::wstring::npos ? std::wstring::npos : tab - start));
        if (tab == std::wstring::npos) break;
        start = tab + 1;
    }
    return parts;
}

// القص المحفوظ (بآخر السطر، 4 أرقام). الملفات القديمة ما فيها: بيضل صفر
void ReadCrop(SceneLayer& l, const std::vector<std::wstring>& p, size_t start)
{
    if (p.size() < start + 4)
        return;
    l.cropL = std::clamp(_wtof(p[start].c_str()), 0.0, 0.95);
    l.cropT = std::clamp(_wtof(p[start + 1].c_str()), 0.0, 0.95);
    l.cropR = std::clamp(_wtof(p[start + 2].c_str()), 0.0, 0.95 - l.cropL);
    l.cropB = std::clamp(_wtof(p[start + 3].c_str()), 0.0, 0.95 - l.cropT);
}

bool LoadScenes()
{
    const std::wstring path = ScenesPath();
    FILE* f = nullptr;
    if (path.empty() || _wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f)
        return false;

    const std::vector<CaptureTarget> targets = ListCaptureTargets(g_hwnd);
    int active = 0;
    wchar_t buf[4096];
    while (fgetws(buf, 4096, f))
    {
        std::wstring line = buf;
        while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r'))
            line.pop_back();
        const std::vector<std::wstring> p = SplitTabs(line);
        if (p.empty())
            continue;

        if (p[0] == L"A" && p.size() >= 2)
            active = _wtoi(p[1].c_str());
        else if (p[0] == L"S" && p.size() >= 2)
        {
            g_scenes.push_back({ p[1], {} });
            if (p.size() >= 4)                     // اختصار المشهد (الملفات القديمة ما فيها)
                g_scenes.back().hotkey = { (UINT)wcstoul(p[2].c_str(), nullptr, 10) & 0xF,
                                           (UINT)wcstoul(p[3].c_str(), nullptr, 10) & 0xFF };
        }
        else if (p[0] == L"M" && p.size() >= 13 && !g_scenes.empty())
        {
            SceneLayer l;
            l.id          = g_scene.nextId++;
            l.kind        = (LayerKind)std::clamp(_wtoi(p[1].c_str()), 1, 4);
            l.x           = _wtof(p[2].c_str());
            l.y           = _wtof(p[3].c_str());
            l.w           = _wtof(p[4].c_str());
            l.h           = _wtof(p[5].c_str());
            l.visible     = p[6] == L"1";
            l.shape       = std::clamp(_wtoi(p[7].c_str()), 0, 2);
            l.mirror      = p[8] == L"1";
            l.color       = (unsigned)wcstoul(p[9].c_str(), nullptr, 10);
            l.background  = p[10] == L"1";
            l.source      = p[11];
            l.target.name = p[12];
            l.target.isMonitor = false;
            ReadCrop(l, p, 13);
            if (p.size() >= 22)             // مفتاح الكروما
            {
                l.chroma       = p[17] == L"1";
                l.chromaColor  = (unsigned)wcstoul(p[18].c_str(), nullptr, 10) & 0xFFFFFF;
                l.chromaSim    = std::clamp((float)_wtof(p[19].c_str()), 0.01f, 1.0f);
                l.chromaSmooth = std::clamp((float)_wtof(p[20].c_str()), 0.005f, 0.5f);
                l.chromaSpill  = std::clamp((float)_wtof(p[21].c_str()), 0.0f, 1.0f);
            }
            if (p.size() >= 24)             // صفحة الويب: حجم الصفحة
            {
                l.srcW = std::clamp(_wtoi(p[22].c_str()), 0, 3840);
                l.srcH = std::clamp(_wtoi(p[23].c_str()), 0, 3840);
            }
            if (l.kind == LayerKind::Web && (l.srcW < 16 || l.srcH < 16))
            {
                l.srcW = 1920;
                l.srcH = 1080;
            }
            if (l.w > 1 && l.h > 1)
                g_scenes.back().layers.push_back(l);
        }
        else if (p[0] == L"P" && p.size() >= 5 && !g_scenes.empty())   // صوت برنامج
        {
            AppAudioEntry a;
            a.volume  = std::clamp(_wtoi(p[1].c_str()), 0, 200) / 100.0f;
            a.muted   = p[2] == L"1";
            a.exePath = p[3];
            a.name    = p[4];
            if (!a.exePath.empty())
                g_scenes.back().apps.push_back(a);
        }
        else if (p[0] == L"L" && p.size() >= 14 && !g_scenes.empty())
        {
            SceneLayer l;
            l.id      = g_scene.nextId++;
            l.x       = _wtof(p[6].c_str());
            l.y       = _wtof(p[7].c_str());
            l.w       = _wtof(p[8].c_str());
            l.h       = _wtof(p[9].c_str());
            l.visible = p[10] == L"1";
            CaptureTarget& t = l.target;
            t.isMonitor = (p[1] == L"m");
            t.rect      = { _wtol(p[2].c_str()), _wtol(p[3].c_str()), _wtol(p[4].c_str()), _wtol(p[5].c_str()) };
            t.className = p[11];
            t.exePath   = p[12];
            t.name      = p[13];
            ReadCrop(l, p, 14);
            if (t.isMonitor)
            {
                // نفس الشاشة إذا لسا موصولة، وإلا الرئيسية
                const CaptureTarget* match = nullptr;
                for (const CaptureTarget& c : targets)
                    if (c.isMonitor && EqualRect(&c.rect, &t.rect)) match = &c;
                if (!match)
                    for (const CaptureTarget& c : targets)
                        if (c.isMonitor && c.isPrimary) match = &c;
                if (match)
                {
                    const std::wstring keepName = t.name;
                    t = *match;
                    if (l.HasCrop() && keepName == L"جزء من الشاشة")
                        t.name = keepName;
                }
            }
            else
            {
                t.hwnd = FindMatchingWindow(t, g_hwnd);    // إذا مش مفتوحة بتضل "مغلقة" لحتى تنفتح
                l.lost = (t.hwnd == nullptr);
            }
            if (l.w > 1 && l.h > 1)
                g_scenes.back().layers.push_back(l);
        }
    }
    fclose(f);

    if (g_scenes.empty())
        return false;
    g_activeScene = std::clamp(active, 0, (int)g_scenes.size() - 1);
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    g_scene.layers = g_scenes[g_activeScene].layers;
    return true;
}

// النوافذ اللي تغيّر شكلها (مثلاً لعبة انتقلت لملء الشاشة، أو انضافت وهي مصغّرة):
// إذا تغيّرت نسبة العرض للطول، منعدّل الطبقة لحتى ما تنمطّ الصورة
// (منحافظ على العرض والمنتصف، وإذا طلعت أطول من اللوحة منصغّرها لتفوت)
void FollowWindowAspect()
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const double cw = g_scene.canvasW, ch = g_scene.canvasH;
        for (SceneLayer& l : g_scene.layers)
        {
            if (l.kind != LayerKind::Capture || l.target.isMonitor || l.lost || g_drag != Drag::None)
                continue;
            int w = 0, h = 0;
            if (!GetCaptureSize(l.target, w, h) || w < 200 || h < 150)
                continue;                       // مصغّرة أو حجم مش حقيقي
            const bool first = (l.srcW == 0);
            if (!first && w == l.srcW && h == l.srcH)
                continue;
            l.srcW = w;
            l.srcH = h;
            // النسبة الصحيحة (بعد القص) والنسبة الحالية للطبقة
            const double want = (w * (1 - l.cropL - l.cropR)) / std::max(1.0, h * (1 - l.cropT - l.cropB));
            const double have = l.w / std::max(1.0, l.h);
            if (std::fabs(want / have - 1) < 0.03)
                continue;                       // تقريباً نفسها
            const double cx = l.x + l.w / 2, cy = l.y + l.h / 2;
            double nw = l.w, nh = l.w / want;
            if (nh > ch) { nh = ch; nw = nh * want; }
            if (nw > cw) { nw = cw; nh = nw / want; }
            l.w = nw;  l.h = nh;
            l.x = cx - nw / 2;  l.y = cy - nh / 2;
            changed = true;
        }
    }
    if (changed)
    {
        g_previewValid = false;
        SaveScenes();
    }
}

// النوافذ المغلقة: إذا رجعت انفتحت منرجّع نربطها لحالها
void ReconnectLostWindows()
{
    auto fix = [](std::vector<SceneLayer>& layers)
    {
        for (SceneLayer& l : layers)
            if (l.kind == LayerKind::Capture && !l.target.isMonitor && !IsWindow(l.target.hwnd))
                if (HWND h = FindMatchingWindow(l.target, g_hwnd))
                {
                    l.target.hwnd = h;
                    l.lost = false;
                }
    };
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        fix(g_scene.layers);
    }
    for (int i = 0; i < (int)g_scenes.size(); ++i)
        if (i != g_activeScene)
            fix(g_scenes[i].layers);
}

void RenameScene(int index)
{
    std::wstring name = g_scenes[index].name;
    if (InputBox(L"اسم المشهد", name))
    {
        // منشيل المسافات من الأطراف
        const size_t a = name.find_first_not_of(L' ');
        const size_t b = name.find_last_not_of(L' ');
        name = (a == std::wstring::npos) ? L"" : name.substr(a, b - a + 1);
        if (!name.empty())
            g_scenes[index].name = name;
        SyncSceneHotkeys();       // الاسم الجديد بنافذة الاختصارات
        SaveScenes();
    }
}

void ShowSceneMenu(int index, POINT pt)
{
    std::vector<std::wstring> items = { L"إعادة التسمية", L"تكرار المشهد" };
    if (g_scenes.size() > 1)
    {
        items.push_back(L"");
        items.push_back(L"حذف المشهد");
    }
    const int cmd = ShowMenu(items, -1, pt);
    if (cmd == 0) RenameScene(index);
    if (cmd == 1) DuplicateScene(index);
    if (cmd == 3) DeleteScene(index);
    if (cmd >= 0) SaveScenes();
}
