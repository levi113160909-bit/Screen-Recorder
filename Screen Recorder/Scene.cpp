// =============================================================
//  Scene.cpp
// =============================================================
#include "Scene.h"
#include "Gpu.h"
#include "Media.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#pragma comment(lib, "msimg32.lib")     // AlphaBlend

Compositor::Compositor(bool drawCursor) : m_cursor(drawCursor) {}

Compositor::~Compositor()
{
    for (auto& [id, m] : m_media)
        if (m.bmp) DeleteObject(m.bmp);
    if (m_dib)
    {
        SelectObject(m_dc, m_oldDib);
        DeleteObject(m_dib);
    }
    if (m_dc)
        DeleteDC(m_dc);
}

void Compositor::Compose(const std::vector<SceneLayer>& layers, int canvasW, int canvasH, const ComposeParams& params)
{
    // أول مرة: منجرّب نشغّل محرّك كرت الشاشة (لازم بنفس الخيط اللي رح يستعمله)
    if (!m_gpuTried)
    {
        m_gpuTried = true;
        m_gpu = GpuCompositor::Create(m_cursor);
    }

    if (m_gpu)
    {
        if (m_gpu->Compose(layers, canvasW, canvasH, params))
            return;
        m_gpu.reset();        // صار خطأ بكرت الشاشة: منكمّل بالطريقة القديمة
        if (params.lostIds) params.lostIds->clear();
    }
    ComposeGdi(layers, canvasW, canvasH, params);
}

// ---------------- الطريقة القديمة (GDI) ----------------
void Compositor::ComposeGdi(const std::vector<SceneLayer>& layers, int canvasW, int canvasH, const ComposeParams& p)
{
    const int outW = p.outW, outH = p.outH;
    if (!m_dc)
    {
        HDC screen = GetDC(nullptr);
        m_dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
    }
    if (outW != m_dibW || outH != m_dibH)
    {
        if (m_dib)
        {
            SelectObject(m_dc, m_oldDib);
            DeleteObject(m_dib);
        }
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = outW;
        bmi.bmiHeader.biHeight      = -outH;          // من فوق لتحت
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        m_dib    = CreateDIBSection(m_dc, &bmi, DIB_RGB_COLORS, &m_bits, nullptr, 0);
        m_oldDib = SelectObject(m_dc, m_dib);
        m_dibW = outW;
        m_dibH = outH;
        m_haveFrame = false;
    }
    if (!m_dib)
        return;

    // الانتقال: منحفظ آخر صورة من المشهد القديم
    const size_t bytes = (size_t)outW * outH * 4;
    if (p.fade.id != m_fadeId)
    {
        m_fadeId = p.fade.id;
        if (m_haveFrame && p.fade.progress < 1)
            m_frozen.assign((BYTE*)m_bits, (BYTE*)m_bits + bytes);
        else
            m_frozen.clear();
    }
    if (p.fade.progress >= 1)
        m_frozen.clear();

    // 1) الخلفية سودا
    RECT all{ 0, 0, outW, outH };
    FillRect(m_dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // 2) منمسح "المصوّرين" تبع الطبقات اللي انحذفت
    for (auto it = m_grabbers.begin(); it != m_grabbers.end();)
    {
        bool exists = false;
        for (const SceneLayer& l : layers)
            if (l.id == it->first && l.kind == LayerKind::Capture) { exists = true; break; }
        it = exists ? std::next(it) : m_grabbers.erase(it);
    }
    for (auto it = m_media.begin(); it != m_media.end();)
    {
        bool exists = false;
        for (const SceneLayer& l : layers)
            if (l.id == it->first && l.kind != LayerKind::Capture) { exists = true; break; }
        if (!exists && it->second.bmp) DeleteObject(it->second.bmp);
        it = exists ? std::next(it) : m_media.erase(it);
    }

    // 3) منرسم الطبقات من تحت لفوق
    const double vx = p.viewW > 0 ? p.viewX : 0, vy = p.viewW > 0 ? p.viewY : 0;
    const double vw = p.viewW > 0 ? p.viewW : canvasW, vh = p.viewH > 0 ? p.viewH : canvasH;
    const double sx = outW / vw;
    const double sy = outH / vh;
    for (const SceneLayer& l : layers)
    {
        const int x = (int)std::lround((l.x - vx) * sx);
        const int y = (int)std::lround((l.y - vy) * sy);
        const int w = (int)std::lround(l.w * sx);
        const int h = (int)std::lround(l.h * sy);
        if (l.kind != LayerKind::Capture)
        {
            DrawMediaGdi(l, x, y, w, h, p.lostIds);
            continue;
        }
        if (!l.visible)
            continue;
        auto& grabber = m_grabbers[l.id];
        if (!grabber)
            grabber = std::make_unique<FrameGrabber>();
        if (!grabber->Grab(l.target, m_dc, x, y, w, h, m_cursor, l.cropL, l.cropT, l.cropR, l.cropB) && p.lostIds)
            p.lostIds->push_back(l.id);
    }
    GdiFlush();
    m_haveFrame = true;

    // 4) التلاشي: خلط القديم مع الجديد
    BYTE* bits = (BYTE*)m_bits;
    if (!m_frozen.empty() && m_frozen.size() == bytes && p.fade.progress < 1)
    {
        const int a = (int)(p.fade.progress * 256);
        for (size_t i = 0; i < bytes; ++i)
            bits[i] = (BYTE)((m_frozen[i] * (256 - a) + bits[i] * a) >> 8);
    }

    // 5) منسخ النتيجة لـ out
    for (int y = 0; y < outH; ++y)
    {
        const BYTE* src = bits + (size_t)y * outW * 4;
        BYTE*       dst = p.out + (size_t)(p.bottomUp ? (outH - 1 - y) : y) * p.stride;
        memcpy(dst, src, (size_t)outW * 4);
    }
}

// الكاميرا/الصورة/النص بالطريقة القديمة
void Compositor::DrawMediaGdi(const SceneLayer& l, int x, int y, int w, int h, std::vector<int>* lostIds)
{
    if (!l.visible)        // مخفية: ما منفتح الكاميرا أصلاً (بينطفى ضوّها)
        return;
    bool missing = false;
    std::shared_ptr<const MediaFrame> f = GetLayerFrame(l, &missing);
    if (missing && lostIds)
        lostIds->push_back(l.id);
    if (!l.visible || !f || w <= 0 || h <= 0)
        return;

    GdiMedia& m = m_media[l.id];
    if (m.version != f->version || !m.bmp)
    {
        if (!m.bmp || m.w != f->width || m.h != f->height)
        {
            if (m.bmp) DeleteObject(m.bmp);
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = f->width;
            bmi.bmiHeader.biHeight      = -f->height;
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            m.bmp = CreateDIBSection(m_dc, &bmi, DIB_RGB_COLORS, &m.bits, nullptr, 0);
            m.w = f->width;
            m.h = f->height;
        }
        if (!m.bmp)
            return;
        memcpy(m.bits, f->pixels.data(), f->pixels.size());
        m.version = f->version;
    }

    // القص (Alt + سحب) أول شي
    int sx = (int)std::lround(m.w * l.cropL), sy = (int)std::lround(m.h * l.cropT);
    int sw = std::max(1, (int)std::lround(m.w * (1 - l.cropL - l.cropR)));
    int sh = std::max(1, (int)std::lround(m.h * (1 - l.cropT - l.cropB)));
    // الكاميرا: منقص لتملا المكان بدون مط
    if (l.kind == LayerKind::Camera)
    {
        const double dstAspect = (double)w / h, srcAspect = (double)sw / sh;
        if (srcAspect > dstAspect) { const int nw = (int)(sh * dstAspect); sx += (sw - nw) / 2; sw = nw; }
        else                       { const int nh = (int)(sw / dstAspect); sy += (sh - nh) / 2; sh = nh; }
    }

    HRGN clip = nullptr;
    if (l.shape == SHAPE_CIRCLE)       clip = CreateEllipticRgn(x, y, x + w + 1, y + h + 1);
    else if (l.shape == SHAPE_ROUNDED) { const int r = std::min(w, h) / 4; clip = CreateRoundRectRgn(x, y, x + w + 1, y + h + 1, r, r); }
    if (clip) SelectClipRgn(m_dc, clip);

    HDC src = CreateCompatibleDC(m_dc);
    HGDIOBJ old = SelectObject(src, m.bmp);
    SetStretchBltMode(m_dc, HALFTONE);
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(m_dc, x, y, w, h, src, sx, sy, sw, sh, bf);
    SelectObject(src, old);
    DeleteDC(src);

    if (clip)
    {
        SelectClipRgn(m_dc, nullptr);
        DeleteObject(clip);
    }
}
