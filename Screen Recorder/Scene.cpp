// =============================================================
//  Scene.cpp
// =============================================================
#include "Scene.h"
#include "Gpu.h"
#include <cmath>
#include <cstring>

Compositor::Compositor(bool drawCursor) : m_cursor(drawCursor) {}

Compositor::~Compositor()
{
    if (m_dib)
    {
        SelectObject(m_dc, m_oldDib);
        DeleteObject(m_dib);
    }
    if (m_dc)
        DeleteDC(m_dc);
}

void Compositor::Compose(const std::vector<SceneLayer>& layers, int canvasW, int canvasH,
                         BYTE* out, int outW, int outH, int stride, bool bottomUp, std::vector<int>* lostIds)
{
    // أول مرة: منجرّب نشغّل محرّك كرت الشاشة (لازم بنفس الخيط اللي رح يستعمله)
    if (!m_gpuTried)
    {
        m_gpuTried = true;
        m_gpu = GpuCompositor::Create(m_cursor);
    }

    if (m_gpu)
    {
        if (m_gpu->Compose(layers, canvasW, canvasH, out, outW, outH, stride, bottomUp, lostIds))
            return;
        m_gpu.reset();        // صار خطأ بكرت الشاشة: منكمّل بالطريقة القديمة
        if (lostIds) lostIds->clear();
    }
    ComposeGdi(layers, canvasW, canvasH, out, outW, outH, stride, bottomUp, lostIds);
}

// ---------------- الطريقة القديمة (GDI) ----------------
void Compositor::ComposeGdi(const std::vector<SceneLayer>& layers, int canvasW, int canvasH,
                            BYTE* out, int outW, int outH, int stride, bool bottomUp, std::vector<int>* lostIds)
{
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
    }
    if (!m_dib)
        return;

    // 1) الخلفية سودا
    RECT all{ 0, 0, outW, outH };
    FillRect(m_dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // 2) منمسح "المصوّرين" تبع الطبقات اللي انحذفت
    for (auto it = m_grabbers.begin(); it != m_grabbers.end();)
    {
        bool exists = false;
        for (const SceneLayer& l : layers)
            if (l.id == it->first) { exists = true; break; }
        it = exists ? std::next(it) : m_grabbers.erase(it);
    }

    // 3) منرسم الطبقات من تحت لفوق
    const double sx = (double)outW / canvasW;
    const double sy = (double)outH / canvasH;
    for (const SceneLayer& l : layers)
    {
        if (!l.visible)
            continue;
        auto& grabber = m_grabbers[l.id];
        if (!grabber)
            grabber = std::make_unique<FrameGrabber>();
        const int x = (int)std::lround(l.x * sx);
        const int y = (int)std::lround(l.y * sy);
        const int w = (int)std::lround(l.w * sx);
        const int h = (int)std::lround(l.h * sy);
        if (!grabber->Grab(l.target, m_dc, x, y, w, h, m_cursor) && lostIds)
            lostIds->push_back(l.id);
    }
    GdiFlush();

    // 4) منسخ النتيجة لـ out
    for (int y = 0; y < outH; ++y)
    {
        const BYTE* src = (const BYTE*)m_bits + (size_t)y * outW * 4;
        BYTE*       dst = out + (size_t)(bottomUp ? (outH - 1 - y) : y) * stride;
        memcpy(dst, src, (size_t)outW * 4);
    }
}
