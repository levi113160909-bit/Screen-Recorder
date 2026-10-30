// =============================================================
//  SceneLayers.cpp - طبقات المشهد: إضافة المصادر وحذفها وقوائمها (شاشة، نافذة، كاميرا، صورة، نص، ويب...)
// =============================================================
#include "App.h"

const unsigned TEXT_COLORS[] = { 0xFFFFFFFF, 0xFF0E1014, 0xFFE5484D, 0xFFF5C518, 0xFF3DD68C, 0xFF4F8CFF, 0xFFFF4FA3 };

const wchar_t* TEXT_COLOR_NAMES[] = { L"أبيض", L"أسود", L"أحمر", L"أصفر", L"أخضر", L"أزرق", L"وردي" };

int FindLayer(int id)     // لازم يكون القفل ممسوك
{
    for (int i = 0; i < (int)g_scene.layers.size(); ++i)
        if (g_scene.layers[i].id == id)
            return i;
    return -1;
}

// مكان الطبقة: "ملاءمة" = أكبر حجم بيوسع بدون ما يتمطّ، بالنص
// الحجم الطبيعي لأي طبقة (الشاشة/النافذة ، الكاميرا ، الصورة ، النص)
bool LayerNaturalSize(const SceneLayer& l, int& w, int& h)
{
    const bool ok = l.kind == LayerKind::Capture ? GetCaptureSize(l.target, w, h) : MediaNaturalSize(l, w, h);
    if (ok && l.kind == LayerKind::Camera && l.shape == SHAPE_CIRCLE)
        w = h = std::min(w, h);              // الدائرة مربّعة
    return ok && w > 0 && h > 0;
}

void FitLayer(SceneLayer& l, int cw, int ch)
{
    int sw = 0, sh = 0;
    if (!LayerNaturalSize(l, sw, sh))
    {
        sw = cw;
        sh = ch;
    }
    sw = std::max(1, (int)std::lround(sw * (1 - l.cropL - l.cropR)));   // الجزء الباقي بعد القص
    sh = std::max(1, (int)std::lround(sh * (1 - l.cropT - l.cropB)));
    const double s = std::min((double)cw / sw, (double)ch / sh);
    l.w = sw * s;
    l.h = sh * s;
    l.x = (cw - l.w) / 2;
    l.y = (ch - l.h) / 2;
}

void AddLayer(const CaptureTarget& target)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    SceneLayer l;
    l.id     = g_scene.nextId++;
    l.target = target;
    FitLayer(l, g_scene.canvasW, g_scene.canvasH);
    g_scene.layers.push_back(l);          // الجديدة فوق الكل
    g_selectedLayer = l.id;
}

// "جزء من الشاشة": منلتقط الشاشة اللي فيها المنطقة ، ومنقصّ عليها (القص على كرت الشاشة: خفيف)
bool RegionToLayer(const RECT& r, SceneLayer& l)
{
    const POINT c{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    for (const CaptureTarget& t : ListCaptureTargets(g_hwnd))
    {
        if (!t.isMonitor || !PtInRect(&t.rect, c))
            continue;
        const double mw = t.rect.right - t.rect.left, mh = t.rect.bottom - t.rect.top;
        RECT in;
        IntersectRect(&in, &r, &t.rect);            // المنطقة جوّا هالشاشة بس
        l.target = t;
        l.target.name = L"جزء من الشاشة";
        l.cropL = (in.left - t.rect.left) / mw;
        l.cropT = (in.top - t.rect.top) / mh;
        l.cropR = (t.rect.right - in.right) / mw;
        l.cropB = (t.rect.bottom - in.bottom) / mh;
        return true;
    }
    return false;
}

void AddRegionLayer()
{
    RECT r;
    if (!SelectScreenRegion(g_hwnd, r))
        return;
    SceneLayer l;
    if (!RegionToLayer(r, l))
        return;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        l.id = g_scene.nextId++;
        FitLayer(l, g_scene.canvasW, g_scene.canvasH);
        g_scene.layers.push_back(l);
        g_selectedLayer = l.id;
    }
    SaveScenes();
}

// تحديد المنطقة من جديد (من قائمة الطبقة): منحافظ على مكانها وطولها
void ReselectRegion(int id)
{
    RECT r;
    if (!SelectScreenRegion(g_hwnd, r))
        return;
    SceneLayer fresh;
    if (!RegionToLayer(r, fresh))
        return;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return;
        SceneLayer& l = g_scene.layers[i];
        l.target = fresh.target;
        l.cropL = fresh.cropL; l.cropT = fresh.cropT; l.cropR = fresh.cropR; l.cropB = fresh.cropB;
        const double rw = r.right - r.left, rh = std::max<LONG>(1, r.bottom - r.top);
        const double cx = l.x + l.w / 2;
        l.w = l.h * rw / rh;
        l.x = cx - l.w / 2;
    }
    SaveScenes();
}

// كاميرا / صورة / نص: منحطّها بمكان منطقي حسب نوعها
void AddMediaLayer(SceneLayer l)
{
    int nw = 0, nh = 0;
    if (!LayerNaturalSize(l, nw, nh))       // برّا القفل (الكاميرا بتاخد لحظة لتفتح)
    {
        nw = 1280;
        nh = 720;
    }
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const double cw = g_scene.canvasW, ch = g_scene.canvasH;
    l.id = g_scene.nextId++;
    if (l.kind == LayerKind::Camera)          // بالزاوية تحت يمين
    {
        l.w = cw * 0.26;
        l.h = l.w * nh / nw;
        l.x = cw - l.w - cw * 0.025;
        l.y = ch - l.h - ch * 0.04;
    }
    else if (l.kind == LayerKind::Image)      // بالنص، مش أكبر من 60% من اللوحة
    {
        const double k = std::min(1.0, std::min(cw * 0.6 / nw, ch * 0.6 / nh));
        l.w = nw * k;
        l.h = nh * k;
        l.x = (cw - l.w) / 2;
        l.y = (ch - l.h) / 2;
    }
    else if (l.kind == LayerKind::Web)
    {
        if (std::abs((double)nw / nh - cw / ch) < 0.02)      // بنفس شكل الفيديو (تنبيهات): فوق كل الإطار
        {
            l.x = 0; l.y = 0; l.w = cw; l.h = ch;
        }
        else                                                  // دردشة وأمثالها: تحت يسار
        {
            const double k = std::min({ 1.0, cw * 0.9 / nw, ch * 0.9 / nh });
            l.w = nw * k;
            l.h = nh * k;
            l.x = cw * 0.02;
            l.y = ch - l.h - ch * 0.04;
        }
    }
    else                                       // النص: تحت بالنص
    {
        double k = ch * 0.10 / nh;
        k = std::min(k, cw * 0.9 / nw);
        l.w = nw * k;
        l.h = nh * k;
        l.x = (cw - l.w) / 2;
        l.y = ch * 0.84 - l.h / 2;
    }
    g_scene.layers.push_back(l);
    g_selectedLayer = l.id;
}

bool PickImageFile(std::wstring& path)
{
    wchar_t buf[MAX_PATH * 2] = {};
    if (!path.empty())
        wcsncpy_s(buf, path.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"الصور\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.tif;*.tiff\0كل الملفات\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH * 2;
    ofn.lpstrTitle  = L"اختر صورة";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return false;
    path = buf;
    return true;
}

void RemoveLayer(int id)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(id);
    if (i >= 0)
        g_scene.layers.erase(g_scene.layers.begin() + i);
    if (g_selectedLayer == id)
        g_selectedLayer = 0;
}

void PrimaryMonitorSize(int& w, int& h)
{
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi))
    {
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        return;
    }
    w = 1920;
    h = 1080;
}

// تغيير حجم اللوحة: منكبّر/منصغّر الطبقات معها
void ApplyCanvasChoice()
{
    int w = 0, h = 0;
    PrimaryMonitorSize(w, h);
    if (CANVAS_H[g_canvasChoice] > 0)     // 1440p / 1080p / 720p = 16:9
    {
        h = CANVAS_H[g_canvasChoice];
        w = h * 16 / 9;
    }
    w &= ~1;
    h &= ~1;

    std::lock_guard<std::mutex> lock(g_scene.mutex);
    if (w == g_scene.canvasW && h == g_scene.canvasH)
        return;
    const double sx = (double)w / g_scene.canvasW, sy = (double)h / g_scene.canvasH;
    for (SceneLayer& l : g_scene.layers)
    {
        l.x *= sx; l.w *= sx;
        l.y *= sy; l.h *= sy;
    }
    g_scene.canvasW = w;
    g_scene.canvasH = h;
    g_previewValid = false;
}

// عمليات القائمة (كبسة يمين على طبقة)
void LayerCommand(int id, int cmd)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(id);
    if (i < 0)
        return;
    SceneLayer& l = g_scene.layers[i];
    const int cw = g_scene.canvasW, ch = g_scene.canvasH;
    switch (cmd)
    {
    case 0: FitLayer(l, cw, ch); break;                                        // ملاءمة
    case 1: l.x = 0; l.y = 0; l.w = cw; l.h = ch; break;                        // تمديد
    case 2: { int sw, sh; if (LayerNaturalSize(l, sw, sh)) { l.w = sw * (1 - l.cropL - l.cropR); l.h = sh * (1 - l.cropT - l.cropB); }
              l.x = (cw - l.w) / 2; l.y = (ch - l.h) / 2; break; }               // الحجم الأصلي
    case 3: l.x = (cw - l.w) / 2; l.y = (ch - l.h) / 2; break;                  // توسيط
    case 5: if (i + 1 < (int)g_scene.layers.size()) std::swap(g_scene.layers[i], g_scene.layers[i + 1]); break;  // للأمام
    case 6: if (i > 0) std::swap(g_scene.layers[i], g_scene.layers[i - 1]); break;                               // للخلف
    case 8: l.visible = !l.visible; break;
    }
}

// بعد تغيير النص/الخلفية/الصورة: منظبط العرض حسب الشكل الجديد (منحافظ على الطول والنص)
void RefitAspect(int id)
{
    SceneLayer copy;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return;
        copy = g_scene.layers[i];
    }
    int nw, nh;
    if (!LayerNaturalSize(copy, nw, nh))
        return;
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(id);
    if (i < 0) return;
    SceneLayer& l = g_scene.layers[i];
    const double cx = l.x + l.w / 2;
    l.w = l.h * (nw * (1 - l.cropL - l.cropR)) / (nh * (1 - l.cropT - l.cropB));
    l.x = cx - l.w / 2;
}

// حجم صفحة الويب: بحجم الفيديو (تنبيهات) ، دردشة ، متوسط ، أو مخصّص
bool PickWebSize(int& w, int& h, POINT pt, const std::wstring& url)
{
    int cw, ch;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        cw = (int)g_scene.canvasW;
        ch = (int)g_scene.canvasH;
    }
    const int sizes[][2] = { { cw, ch }, { 400, 600 }, { 800, 600 } };
    auto dims = [](int a, int b) { return L"\u2066" + std::to_wstring(a) + L" × " + std::to_wstring(b) + L"\u2069"; };
    std::vector<std::wstring> items = {
        L"تنبيهات وتراكبات — بحجم الفيديو " + dims(cw, ch),
        L"دردشة — " + dims(400, 600),
        L"حجم متوسط — " + dims(800, 600),
        L"حجم مخصّص…" };
    int checked = -1;
    for (int k = 0; k < 3; ++k)
        if (sizes[k][0] == w && sizes[k][1] == h) checked = k;
    if (checked < 0 && w <= 0)                         // صفحة جديدة: منخمّن من الرابط
    {
        std::wstring low = url;
        for (wchar_t& c : low) c = (wchar_t)towlower(c);
        checked = low.find(L"chat") != std::wstring::npos ? 1 : 0;
    }
    const int pick = ShowMenu(items, checked, pt);
    if (pick < 0)
        return false;
    if (pick < 3)
    {
        w = sizes[pick][0];
        h = sizes[pick][1];
        return true;
    }
    std::wstring text = std::to_wstring(w > 0 ? w : 1280) + L"x" + std::to_wstring(h > 0 ? h : 720);
    if (!InputBox(L"العرض × الارتفاع (مثل 1280x720)", text, false, true, 20))
        return false;
    int a = 0, b = 0;
    if (swscanf_s(text.c_str(), L"%d%*[^0-9]%d", &a, &b) != 2 || a < 16 || b < 16 || a > 3840 || b > 3840)
    {
        ShowMessage(L"الحجم غير صالح (من 16 إلى 3840)", true);
        return false;
    }
    w = a;
    h = b;
    return true;
}

// أوامر خاصة بالكاميرا والصورة والنص
void LayerMediaCommand(int id, int code, POINT pt)
{
    SceneLayer copy;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return;
        copy = g_scene.layers[i];
    }
    auto modify = [&](auto&& fn)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i >= 0) fn(g_scene.layers[i]);
    };

    switch (code)
    {
    case 106:   // مفتاح الكروما
    {
        ChromaHooks h;
        h.get = [id](SceneLayer& out)
        {
            std::lock_guard<std::mutex> lock(g_scene.mutex);
            const int i = FindLayer(id);
            if (i < 0) return false;
            out = g_scene.layers[i];
            return true;
        };
        h.modify = [id](const std::function<void(SceneLayer&)>& fn)
        {
            std::lock_guard<std::mutex> lock(g_scene.mutex);
            const int i = FindLayer(id);
            if (i >= 0) fn(g_scene.layers[i]);
        };
        h.save = [] { SaveScenes(); };
        g_panels.Open(std::make_unique<ChromaPanel>(std::move(h)));
        return;
    }
    case 100:   // الشكل
    {
        const int pick = ShowMenu({ L"مستطيل", L"زوايا مدوّرة", L"دائرة" }, copy.shape, pt);
        if (pick < 0) return;
        modify([&](SceneLayer& l)
        {
            l.shape = pick;
            if (pick == SHAPE_CIRCLE && l.kind == LayerKind::Camera)   // الدائرة لازم تكون مربّعة
            {
                const double s = std::min(l.w, l.h), cx = l.x + l.w / 2, cy = l.y + l.h / 2;
                l.w = l.h = s;
                l.x = cx - s / 2;
                l.y = cy - s / 2;
            }
        });
        break;
    }
    case 101:   // مراية
        modify([](SceneLayer& l) { l.mirror = !l.mirror; });
        break;
    case 102:   // تغيير الصورة
    {
        std::wstring path = copy.source;
        if (!PickImageFile(path)) return;
        modify([&](SceneLayer& l) { l.source = path; l.target.name = FileNameOf(path); });
        RefitAspect(id);
        break;
    }
    case 103:   // تعديل النص
    {
        std::wstring text = copy.source;
        if (!InputBox(L"النص", text, false, false, 200) || text.empty()) return;
        modify([&](SceneLayer& l) { l.source = text; l.target.name = text; });
        RefitAspect(id);
        break;
    }
    case 104:   // اللون
    {
        std::vector<std::wstring> names(std::begin(TEXT_COLOR_NAMES), std::end(TEXT_COLOR_NAMES));
        int checked = -1;
        for (int k = 0; k < (int)names.size(); ++k)
            if (TEXT_COLORS[k] == copy.color) checked = k;
        const int pick = ShowMenu(names, checked, pt);
        if (pick < 0) return;
        modify([&](SceneLayer& l) { l.color = TEXT_COLORS[pick]; });
        break;
    }
    case 105:   // الخلفية
        modify([](SceneLayer& l) { l.background = !l.background; });
        RefitAspect(id);
        break;
    case 107:   // تحديث الصفحة
        BrowserReload(copy.source, copy.srcW, copy.srcH);
        return;
    case 108:   // فتح للتفاعل
        BrowserOpenInteractive(copy.source);
        return;
    case 109:   // تغيير الرابط
    {
        std::wstring url = copy.source;
        if (!InputBox(L"رابط الصفحة", url, false, true, 2000)) return;
        url = NormalizeUrl(url);
        if (url.empty()) return;
        modify([&](SceneLayer& l) { l.source = url; l.target.name = UrlDisplayName(url); });
        break;
    }
    case 110:   // حجم الصفحة
    {
        int w = copy.srcW, h = copy.srcH;
        if (!PickWebSize(w, h, pt, copy.source)) return;
        modify([&](SceneLayer& l) { l.srcW = w; l.srcH = h; });
        RefitAspect(id);
        break;
    }
    }
    SaveScenes();
}

// إلغاء القص: المحتوى بيضل بنفس الحجم، والطبقة بترجع تكبر لحجم المصدر كامل
void ResetCrop(int id)
{
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return;
        SceneLayer& l = g_scene.layers[i];
        const double fw = l.w / std::max(0.05, 1 - l.cropL - l.cropR);
        const double fh = l.h / std::max(0.05, 1 - l.cropT - l.cropB);
        l.x -= l.cropL * fw;
        l.y -= l.cropT * fh;
        l.w = fw;
        l.h = fh;
        l.cropL = l.cropT = l.cropR = l.cropB = 0;
    }
    SaveScenes();
}

void ShowLayerMenu(int id, POINT screenPt)
{
    bool visible = true;
    SceneLayer copy;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return;
        visible = g_scene.layers[i].visible;
        copy = g_scene.layers[i];
    }
    std::vector<std::wstring> items = { L"ملاءمة داخل الإطار", L"تمديد لملء الإطار", L"الحجم الأصلي", L"توسيط",
                                        L"", L"نقل إلى الأمام", L"نقل إلى الخلف",
                                        L"", visible ? L"إخفاء" : L"إظهار", L"حذف" };
    std::vector<int> codes(items.size(), -1);
    auto extra = [&](const std::wstring& label, int code) { items.push_back(label); codes.push_back(code); };
    if (copy.kind == LayerKind::Camera)
    {
        extra(L"", -1);
        extra(L"الشكل…", 100);
        extra(copy.mirror ? L"إلغاء عكس الصورة" : L"عكس الصورة (مثل المرآة)", 101);
        extra(copy.chroma ? L"✓   إزالة الخلفية الخضراء…" : L"إزالة الخلفية الخضراء…", 106);
    }
    else if (copy.kind == LayerKind::Image)
    {
        extra(L"", -1);
        extra(L"الشكل…", 100);
        extra(L"تغيير الصورة…", 102);
    }
    else if (copy.kind == LayerKind::Text)
    {
        extra(L"", -1);
        extra(L"تعديل النص…", 103);
        extra(L"لون النص…", 104);
        extra(copy.background ? L"إزالة الخلفية" : L"إضافة خلفية خلف النص", 105);
    }
    else if (copy.kind == LayerKind::Web)
    {
        extra(L"", -1);
        extra(L"تحديث الصفحة", 107);
        extra(L"فتح الصفحة للتفاعل (تسجيل الدخول)…", 108);
        extra(L"تغيير الرابط…", 109);
        extra(L"حجم الصفحة…", 110);
    }

    if (copy.kind == LayerKind::Capture && copy.target.isMonitor)
    {
        extra(L"", -1);
        extra(L"تحديد جزء من الشاشة…", 300);
    }
    if (copy.HasCrop())
    {
        extra(L"", -1);
        extra(L"إلغاء القص", 200);
    }

    const int cmd = ShowMenu(items, -1, screenPt);
    if (cmd < 0) return;
    if (cmd >= 10 && codes[cmd] == 200) ResetCrop(id);
    else if (cmd >= 10 && codes[cmd] == 300) ReselectRegion(id);
    else if (cmd >= 10) { if (codes[cmd] >= 0) LayerMediaCommand(id, codes[cmd], screenPt); }
    else if (cmd == 9) RemoveLayer(id);
    else               LayerCommand(id, cmd);
}

// نافذة "إضافة صوت برنامج" (شوف AppAudioPanel.cpp)
void ShowAddAppAudioMenu(POINT)
{
    g_panels.Open(std::make_unique<AppAudioPanel>(
        [](const std::wstring& exe)
        {
            for (const AppAudioEntry& e : g_appMixer.Entries())
                if (_wcsicmp(e.exePath.c_str(), exe.c_str()) == 0)
                    return true;
            return false;
        },
        [](const AudioApp& app)
        {
            const size_t before = g_appMixer.Count();
            g_appMixer.Add(app);
            if (g_appMixer.Count() > before && !g_appMixer.IsConnected((int)before))
                ShowMessage(L"تعذّر التقاط صوت البرنامج (يتطلّب ويندوز 10 الإصدار 2004 أو أحدث)", true);
            else
                ShowMessage(L"أُضيف صوت " + app.name, false);
            SaveScenes();
        }));
}

// نافذة "إضافة مصدر" (شوف AddSourcePanel.cpp)
void ShowAddSourceMenu(POINT)
{
    AddSourceActions a;
    a.capture = [](const CaptureTarget& t)
    {
        AddLayer(t);
        SaveScenes();
    };
    a.camera = [](const CameraInfo& c)
    {
        SceneLayer l;
        l.kind        = LayerKind::Camera;
        l.source      = c.link;
        l.target.name = c.name;
        l.shape       = SHAPE_ROUNDED;
        AddMediaLayer(l);
        SaveScenes();
    };
    a.image = []
    {
        std::wstring path;
        if (!PickImageFile(path))
            return;
        SceneLayer l;
        l.kind        = LayerKind::Image;
        l.source      = path;
        l.target.name = FileNameOf(path);
        AddMediaLayer(l);
        SaveScenes();
    };
    a.text = []
    {
        std::wstring text = L"سأعود قريباً";
        if (!InputBox(L"النص", text, false, false, 200) || text.empty())
            return;
        SceneLayer l;
        l.kind        = LayerKind::Text;
        l.source      = text;
        l.target.name = text;
        l.background  = true;
        AddMediaLayer(l);
        SaveScenes();
    };
    a.appAudio = [] { ShowAddAppAudioMenu({}); };
    a.region   = [] { AddRegionLayer(); };
    a.web      = []
    {
        if (!BrowserRuntimeAvailable())
        {
            ShowMessage(L"لعرض صفحات الويب يلزم تثبيت «Microsoft Edge WebView2 Runtime» من موقع مايكروسوفت", true);
            return;
        }
        std::wstring url;
        if (!InputBox(L"رابط الصفحة (تنبيهات، دردشة، عدّاد...)", url, false, true, 2000))
            return;
        url = NormalizeUrl(url);
        if (url.empty())
            return;
        int w = 0, h = 0;
        POINT pt;
        GetCursorPos(&pt);
        if (!PickWebSize(w, h, pt, url))
            return;
        SceneLayer l;
        l.kind        = LayerKind::Web;
        l.source      = url;
        l.target.name = UrlDisplayName(url);
        l.srcW        = w;
        l.srcH        = h;
        AddMediaLayer(l);
        SaveScenes();
    };
    g_panels.Open(std::make_unique<AddSourcePanel>(g_hwnd, std::move(a)));
}
