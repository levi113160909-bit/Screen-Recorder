// =============================================================
//  PreviewEdit.cpp - المعاينة: رسم المشهد المصغّر، وتحريك وتكبير الطبقات بالماوس مع الالتصاق
// =============================================================
#include "App.h"

// -------------------------------------------------------------
// المغناطيس (Auto Snap): الحواف بتلزق على حواف اللوحة ونصها،
// وعلى حواف ونص المصادر التانية. Ctrl = بلا مغناطيس
// -------------------------------------------------------------
struct SnapEdges
{
    std::vector<double> x, y;     // كل الخطوط اللي فينا نلزق عليها
};

D2D1_POINT_2F g_dragStart{};

SceneLayer    g_dragOrig;

std::vector<BYTE>     g_previewPixels;

int                   g_previewW = 0, g_previewH = 0;

D2D1_RECT_F           g_previewDst{};      // وين انرسمت اللوحة فعلياً

void UpdatePreview()
{
    if (!g_rt)
        return;

    const float scale = g_dpi / 96.0f;
    const int pw = std::max(2, std::min(1280, (int)((g_previewDst.right - g_previewDst.left) * scale)));
    const int ph = std::max(2, std::min(720,  (int)((g_previewDst.bottom - g_previewDst.top) * scale)));
    if (pw < 8 || ph < 8)
        return;

    if (pw != g_previewW || ph != g_previewH)
    {
        g_previewPixels.assign((size_t)pw * ph * 4, 0);
        g_previewW = pw;
        g_previewH = ph;
        g_previewBitmap.Reset();
    }

    int cw, ch;
    std::vector<SceneLayer> layers;
    std::vector<int> lost;
    ComposeParams cp;
    cp.out = g_previewPixels.data(); cp.outW = pw; cp.outH = ph; cp.stride = pw * 4; cp.lostIds = &lost;
    g_scene.Snapshot(cw, ch, layers, cp.fade);
    g_previewCompositor.Compose(layers, cw, ch, cp);

    // منعلّم الطبقات اللي نوافذها انسكّرت
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (SceneLayer& l : g_scene.layers)
            l.lost = std::find(lost.begin(), lost.end(), l.id) != lost.end();
    }

    if (!g_previewBitmap)
    {
        const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        g_rt->CreateBitmap(D2D1::SizeU(pw, ph), g_previewPixels.data(), pw * 4, props, &g_previewBitmap);
    }
    else
    {
        g_previewBitmap->CopyFromMemory(nullptr, g_previewPixels.data(), pw * 4);
    }
    g_previewValid = (g_previewBitmap != nullptr);
}

// مستطيل الطبقة بإحداثيات الشاشة (DIP)
D2D1_RECT_F LayerToDip(const SceneLayer& l, int cw)
{
    const float s = (g_previewDst.right - g_previewDst.left) / cw;
    return Rect(g_previewDst.left + (float)l.x * s, g_previewDst.top + (float)l.y * s,
                g_previewDst.left + (float)(l.x + l.w) * s, g_previewDst.top + (float)(l.y + l.h) * s);
}

// مكان المقابض الثمانية: 0 فوق-يسار، 1 فوق، 2 فوق-يمين، 3 يمين، 4 تحت-يمين، 5 تحت، 6 تحت-يسار، 7 يسار
D2D1_POINT_2F HandlePos(const D2D1_RECT_F& r, int i)
{
    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    const D2D1_POINT_2F p[8] = { {r.left, r.top}, {cx, r.top}, {r.right, r.top}, {r.right, cy},
                                 {r.right, r.bottom}, {cx, r.bottom}, {r.left, r.bottom}, {r.left, cy} };
    return p[i];
}

// ---------- المعاينة (اللوحة) ----------
void DrawPreview(const D2D1_RECT_F& area)
{
    const bool rec = g_recorder.IsRecording();
    Fill(area, Theme::Panel, 14);
    if (rec)
        Stroke(area, g_recorder.IsPaused() ? Theme::Yellow : Theme::Red, 14, 2);

    // ---- شريط المشاهد (فوق المعاينة) ----
    const float sy = area.top + 12, sh = 34;
    const D2D1_RECT_F strip = Rect(area.left + 12, sy, area.right - 12, sy + sh);
    g_rt->PushAxisAlignedClip(strip, D2D1_ANTIALIAS_MODE_ALIASED);
    float sx = strip.right;
    for (int i = 0; i < (int)g_scenes.size(); ++i)
    {
        const bool active = (i == g_activeScene);
        const float w = std::min(220.0f, TextWidth(g_scenes[i].name, g_fSegment.Get()) + 32);
        const D2D1_RECT_F pill = Rect(sx - w, sy, sx, sy + sh);
        const bool hover = AddHit(pill, IDB_SCENE.Id(i));
        Fill(pill, active ? Theme::Accent : (hover ? Theme::Hover : Theme::Card), 9);
        Text(g_scenes[i].name, g_fSegment.Get(), Rect(pill.left + 12, pill.top, pill.right - 12, pill.bottom),
             active ? Theme::White : Theme::Text);
        if (active && rec)          // نقطة حمرا: هاد المشهد عم يتسجّل
            Circle(pill.right - 7, pill.top + 7, 3.5f, g_recorder.IsPaused() ? Theme::Yellow : Theme::Red);
        sx -= w + 6;
    }
    const D2D1_RECT_F addScene = Rect(sx - 34, sy, sx, sy + sh);
    Fill(addScene, AddHit(addScene, ID_ADD_SCENE) ? Theme::Hover : Theme::Card, 9);
    Tip(ID_ADD_SCENE, L"مشهد جديد");
    Text(Icon::Add, g_fIcon.Get(), addScene, Theme::SubText);
    g_rt->PopAxisAlignedClip();

    const D2D1_RECT_F inner = Rect(area.left + 14, sy + sh + 12, area.right - 14, area.bottom - 46);
    g_previewArea = inner;

    int cw, ch;
    std::vector<SceneLayer> layers;
    g_scene.Snapshot(cw, ch, layers);

    // اللوحة بنسبة الفيديو، بالنص
    const float iw = inner.right - inner.left, ih = inner.bottom - inner.top;
    const float fit = std::min(iw / cw, ih / ch);
    const float dw = cw * fit, dh = ch * fit;
    g_previewDst = Rect(inner.left + (iw - dw) / 2, inner.top + (ih - dh) / 2,
                        inner.left + (iw + dw) / 2, inner.top + (ih + dh) / 2);

    if (g_previewValid && g_previewBitmap)
        g_rt->DrawBitmap(g_previewBitmap.Get(), g_previewDst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else
        Fill(g_previewDst, D2D1::ColorF(0x000000));
    Stroke(g_previewDst, Theme::Border, 0, 1);

    if (layers.empty())
        Text(L"المشهد فارغ — أضف مصدرًا من القائمة", g_fCenter.Get(), g_previewDst, Theme::SubText);

    // إطار الطبقة المختارة ومقابضها
    g_rt->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_ALIASED);
    for (const SceneLayer& l : layers)
    {
        const D2D1_RECT_F r = LayerToDip(l, cw);
        if (l.id == g_selectedLayer)
        {
            // وضع القص (Alt): إطار أخضر، وإطار خفيف للمصدر الكامل لتعرف شو مقصوص
            const bool cropping = (GetKeyState(VK_MENU) & 0x8000) != 0 && (g_drag == Drag::None || g_drag == Drag::Resize);
            const D2D1_COLOR_F c = cropping ? Theme::Green : Theme::Accent;
            if (cropping && l.HasCrop())
            {
                const float fw = (r.right - r.left) / (float)std::max(0.05, 1 - l.cropL - l.cropR);
                const float fh = (r.bottom - r.top) / (float)std::max(0.05, 1 - l.cropT - l.cropB);
                const float fx = r.left - (float)l.cropL * fw, fy = r.top - (float)l.cropT * fh;
                const D2D1_RECT_F full = Rect(fx, fy, fx + fw, fy + fh);
                Fill(Rect(full.left, full.top, full.right, r.top), D2D1::ColorF(0x000000, 0.45f));        // المقصوص فوق
                Fill(Rect(full.left, r.bottom, full.right, full.bottom), D2D1::ColorF(0x000000, 0.45f));  // تحت
                Fill(Rect(full.left, r.top, r.left, r.bottom), D2D1::ColorF(0x000000, 0.45f));            // يسار
                Fill(Rect(r.right, r.top, full.right, r.bottom), D2D1::ColorF(0x000000, 0.45f));          // يمين
                Stroke(full, WithAlpha(Theme::White, 0.35f), 0, 1);
            }
            Stroke(r, c, 0, 2);
            for (int i = 0; i < 8; ++i)
            {
                const D2D1_POINT_2F p = HandlePos(r, i);
                Fill(Rect(p.x - 5, p.y - 5, p.x + 5, p.y + 5), cropping ? c : Theme::White);
                Stroke(Rect(p.x - 5, p.y - 5, p.x + 5, p.y + 5), cropping ? Theme::White : c, 0, 1);
            }
            if (cropping)
                Text(L"قص", g_fSmallLtr.Get(), Rect(r.left, r.top - 22, r.left + 40, r.top - 4), Theme::Green);
        }
        else if (Inside(r, g_mouse) && g_drag == Drag::None)
        {
            Stroke(r, WithAlpha(Theme::Accent, 0.5f), 0, 1);
        }
        else if (g_drag != Drag::None)       // وقت السحب: إطار خفيف لكل المصادر التانية
        {
            Stroke(r, WithAlpha(Theme::White, 0.25f), 0, 1);
        }
    }

    // خطوط المغناطيس (وردية متل برامج التصميم)
    if (g_drag != Drag::None)
    {
        const float s = (g_previewDst.right - g_previewDst.left) / cw;
        g_brush->SetColor(D2D1::ColorF(0xFF4FA3));
        for (double gx : g_guidesX)
        {
            const float x = g_previewDst.left + (float)gx * s;
            g_rt->DrawLine(D2D1::Point2F(x, g_previewDst.top), D2D1::Point2F(x, g_previewDst.bottom), g_brush.Get(), 1.5f);
        }
        for (double gy : g_guidesY)
        {
            const float y = g_previewDst.top + (float)gy * s;
            g_rt->DrawLine(D2D1::Point2F(g_previewDst.left, y), D2D1::Point2F(g_previewDst.right, y), g_brush.Get(), 1.5f);
        }
    }
    g_rt->PopAxisAlignedClip();

    // تحت: الدقة • الإطارات • معدّل البت
    const std::wstring summary = L"الدقة: " + SizeText(cw, ch)
                               + L"  •  " + std::to_wstring(CurrentFps()) + L" إطار/ث"
                               + L"  •  " + MbpsText(CurrentBitrate()) + L" ميغابت/ث";
    Text(summary, g_fSmall.Get(), Rect(area.left + 16, area.bottom - 42, area.right - 16, area.bottom - 14),
         rec ? Theme::Text : Theme::SubText);
    if (g_selectedLayer != 0)        // النصائح بس لما تختار طبقة (وقتها بتلزم)
        Text(L"اسحب للتحريك • المقابض للحجم • Alt للقص • Ctrl بدون التصاق • الزر الأيمن للمزيد", g_fSmallEnd.Get(),
             Rect(area.left + 16, area.bottom - 42, area.right - 16, area.bottom - 14), Theme::Off);
}

int LayerAt(D2D1_POINT_2F p)
{
    int cw, ch;
    std::vector<SceneLayer> layers;
    g_scene.Snapshot(cw, ch, layers);
    for (auto it = layers.rbegin(); it != layers.rend(); ++it)     // من فوق لتحت
        if (it->visible && Inside(LayerToDip(*it, cw), p))
            return it->id;
    return 0;
}

int HandleAt(D2D1_POINT_2F p)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(g_selectedLayer);
    if (i < 0)
        return -1;
    const D2D1_RECT_F r = LayerToDip(g_scene.layers[i], g_scene.canvasW);
    for (int h = 0; h < 8; ++h)
    {
        const D2D1_POINT_2F hp = HandlePos(r, h);
        if (std::fabs(p.x - hp.x) <= 8 && std::fabs(p.y - hp.y) <= 8)
            return h;
    }
    return -1;
}

bool BeginDrag(D2D1_POINT_2F p)
{
    const D2D1_RECT_F zone = Rect(g_previewArea.left - 8, g_previewArea.top - 8, g_previewArea.right + 8, g_previewArea.bottom + 8);
    if (!Inside(zone, p))
        return false;

    const int handle = HandleAt(p);
    int id = g_selectedLayer;
    if (handle < 0)
    {
        id = LayerAt(p);
        g_selectedLayer = id;
        if (id == 0)
            return true;      // كبسة على مكان فاضي = إلغاء التحديد
    }

    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(id);
    if (i < 0)
        return true;
    g_dragOrig   = g_scene.layers[i];
    g_dragStart  = p;
    g_dragHandle = handle;
    g_drag       = handle >= 0 ? Drag::Resize : Drag::Move;
    SetCapture(g_hwnd);
    return true;
}

SnapEdges CollectEdges(int excludeId)   // لازم يكون القفل ممسوك
{
    const double cw = g_scene.canvasW, ch = g_scene.canvasH;
    SnapEdges e;
    e.x = { 0, cw / 2, cw };
    e.y = { 0, ch / 2, ch };
    for (const SceneLayer& l : g_scene.layers)
    {
        if (l.id == excludeId || !l.visible)
            continue;
        e.x.insert(e.x.end(), { l.x, l.x + l.w / 2, l.x + l.w });
        e.y.insert(e.y.end(), { l.y, l.y + l.h / 2, l.y + l.h });
    }
    return e;
}

// بيلزّق قيمة وحدة (حافة) على أقرب خط، وبيرجّع الخط إذا لزق
bool SnapValue(double& v, const std::vector<double>& lines, double range, double* guide)
{
    double best = range;
    bool hit = false;
    for (double line : lines)
    {
        const double d = std::fabs(v - line);
        if (d < best) { best = d; hit = true; if (guide) *guide = line; }
    }
    if (hit) v = *guide;
    return hit;
}

void UpdateDrag(D2D1_POINT_2F p, bool shift, bool noSnap, bool crop)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    g_guidesX.clear();
    g_guidesY.clear();
    const int i = FindLayer(g_dragOrig.id);
    if (i < 0)
        return;
    SceneLayer& l = g_scene.layers[i];
    const SceneLayer& o = g_dragOrig;
    const double s  = (g_previewDst.right - g_previewDst.left) / g_scene.canvasW;   // DIP لكل بكسل
    const double dx = (p.x - g_dragStart.x) / s;
    const double dy = (p.y - g_dragStart.y) / s;
    const double range = noSnap ? 0 : 8 / s;      // المغناطيس بيشتغل من بُعد 8 نقاط على الشاشة
    const SnapEdges edges = CollectEdges(o.id);

    if (g_drag == Drag::Move)
    {
        double x = o.x + dx, y = o.y + dy;

        // منجرّب نلزّق: يسار المصدر، نصّه، ويمينه - ومنختار الأقرب
        auto snapAxis = [&](double& pos, double size, const std::vector<double>& lines, std::vector<double>& guides)
        {
            double bestD = range, bestPos = pos, bestLine = 0;
            bool hit = false;
            for (double offset : { 0.0, size / 2, size })
                for (double line : lines)
                {
                    const double d = std::fabs(pos + offset - line);
                    if (d < bestD) { bestD = d; bestPos = line - offset; bestLine = line; hit = true; }
                }
            if (hit)
            {
                pos = bestPos;
                guides.push_back(bestLine);
            }
        };
        snapAxis(x, o.w, edges.x, g_guidesX);
        snapAxis(y, o.h, edges.y, g_guidesY);
        l.x = x;
        l.y = y;
        return;
    }

    // ---- تغيير الحجم ----
    const int h = g_dragHandle;
    const bool L = (h == 0 || h == 6 || h == 7), R = (h == 2 || h == 3 || h == 4);
    const bool T = (h == 0 || h == 1 || h == 2), B = (h == 4 || h == 5 || h == 6);
    double left = o.x, top = o.y, right = o.x + o.w, bottom = o.y + o.h;
    if (L) left += dx;
    if (R) right += dx;
    if (T) top += dy;
    if (B) bottom += dy;

    // الحافة اللي عم تتحرك بتلزق
    double g = 0;
    if (L && SnapValue(left,   edges.x, range, &g)) g_guidesX.push_back(g);
    if (R && SnapValue(right,  edges.x, range, &g)) g_guidesX.push_back(g);
    if (T && SnapValue(top,    edges.y, range, &g)) g_guidesY.push_back(g);
    if (B && SnapValue(bottom, edges.y, range, &g)) g_guidesY.push_back(g);

    // ---- القص (Alt): الحافة بتتحرك بس المحتوى بيضل مكانه وبنفس حجمه ----
    if (crop)
    {
        // حجم المصدر كامل (قبل القص) على اللوحة
        const double fw = o.w / std::max(0.05, 1 - o.cropL - o.cropR);
        const double fh = o.h / std::max(0.05, 1 - o.cropT - o.cropB);
        const double minW = 16 / fw, minH = 16 / fh;            // أصغر شي 16 بكسل
        double cl = o.cropL, cr = o.cropR, ct = o.cropT, cb = o.cropB;
        // ما منقص أكتر من المصدر، وما منكبّر أكبر منه (القص ما بيصير سالب)
        if (L) cl = std::clamp(o.cropL + (left - o.x) / fw, 0.0, 1 - cr - minW);
        if (R) cr = std::clamp(o.cropR + (o.x + o.w - right) / fw, 0.0, 1 - cl - minW);
        if (T) ct = std::clamp(o.cropT + (top - o.y) / fh, 0.0, 1 - cb - minH);
        if (B) cb = std::clamp(o.cropB + (o.y + o.h - bottom) / fh, 0.0, 1 - ct - minH);
        // مكان المصدر الكامل ثابت، ومنه منحسب الطبقة
        const double fx = o.x - o.cropL * fw, fy = o.y - o.cropT * fh;
        l.cropL = cl;  l.cropR = cr;  l.cropT = ct;  l.cropB = cb;
        l.x = fx + cl * fw;
        l.y = fy + ct * fh;
        l.w = fw * (1 - cl - cr);
        l.h = fh * (1 - ct - cb);
        return;
    }
    l.cropL = o.cropL;  l.cropR = o.cropR;  l.cropT = o.cropT;  l.cropB = o.cropB;

    // الزوايا بتحافظ على النسبة (إلا إذا ضاغط Shift)
    if ((L || R) && (T || B) && !shift && o.h > 0)
    {
        const double aspect = o.w / o.h;
        double nw = right - left, nh = bottom - top;
        if (std::fabs(nw / o.w) > std::fabs(nh / o.h)) { nh = nw / aspect; g_guidesY.clear(); }
        else                                           { nw = nh * aspect; g_guidesX.clear(); }
        if (L) left = right - nw; else right = left + nw;
        if (T) top = bottom - nh; else bottom = top + nh;
    }

    // أصغر حجم 16 بكسل
    if (right - left < 16) { if (L) left = right - 16; else right = left + 16; }
    if (bottom - top < 16) { if (T) top = bottom - 16; else bottom = top + 16; }
    l.x = left;
    l.y = top;
    l.w = right - left;
    l.h = bottom - top;
}

void NudgeSelected(double dx, double dy)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(g_selectedLayer);
    if (i >= 0)
    {
        g_scene.layers[i].x += dx;
        g_scene.layers[i].y += dy;
    }
}
