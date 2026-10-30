// =============================================================
//  ChromaPanel.cpp - نافذة "إزالة الخلفية الخضراء"
// =============================================================
#include "ChromaPanel.h"
#include "Chroma.h"
#include <algorithm>

namespace
{
    enum : int
    {
        ID_TOGGLE  = ID_PANEL_USER,
        ID_PREVIEW = ID_PANEL_USER + 1,
        ID_AUTO    = ID_PANEL_USER + 2,
        ID_GREEN   = ID_PANEL_USER + 3,
        ID_BLUE    = ID_PANEL_USER + 4,
        ID_SIM     = ID_PANEL_USER + 10,
        ID_SMOOTH  = ID_PANEL_USER + 11,
        ID_SPILL   = ID_PANEL_USER + 12
    };
}

void ChromaPanel::DrawBody(float L, float top, float R)
{
    SceneLayer l;
    if (!m_h.get || !m_h.get(l))
    {
        Text(L"طبقة الكاميرا لم تعد موجودة", g_fCenter.Get(), Rect(L, top, R, top + 60), Theme::SubText);
        return;
    }
    float y = top;
    const float lft = L + 4, rgt = R - 4;

    // ---- التشغيل ----
    AddHit(Rect(lft, y, rgt, y + 36), ID_TOGGLE);
    DrawToggle(lft, y + 6, l.chroma, true);
    Text(L"إزالة الخلفية", g_fBody.Get(), Rect(lft + 56, y, rgt, y + 36), Theme::Text);
    y += 46;

    // ---- المعاينة (فوق مربّعات لحتى الشفاف يبيّن) ----
    const float pw = rgt - lft, ph = pw * 9 / 16;
    const D2D1_RECT_F box = Rect(lft, y, rgt, y + ph);
    for (int iy = 0; iy * 12 < ph; ++iy)
        for (int ix = 0; ix * 12 < pw; ++ix)
        {
            const float x0 = box.left + ix * 12.0f, y0 = box.top + iy * 12.0f;
            Fill(Rect(x0, y0, std::min(x0 + 12, box.right), std::min(y0 + 12, box.bottom)),
                 ((ix + iy) & 1) ? D2D1::ColorF(0x3A3F4A) : D2D1::ColorF(0x2A2E37));
        }
    std::shared_ptr<const MediaFrame> f = GetLayerFrame(l);
    if (f && f->width > 0 && g_rt)
    {
        if (!m_bitmap || m_owner != g_rt.Get() || m_frameW != f->width || m_frameH != f->height)
        {
            m_bitmap.Reset();
            m_owner = g_rt.Get();
            const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            g_rt->CreateBitmap(D2D1::SizeU(f->width, f->height), f->pixels.data(), f->width * 4, props, &m_bitmap);
            m_frameW = f->width;
            m_frameH = f->height;
            m_version = f->version;
        }
        else if (m_version != f->version)
        {
            m_bitmap->CopyFromMemory(nullptr, f->pixels.data(), f->width * 4);
            m_version = f->version;
        }
        // بنفس النسبة جوّا الصندوق
        const float s = std::min(pw / f->width, ph / f->height);
        const float dw = f->width * s, dh = f->height * s;
        m_previewDst = Rect(box.left + (pw - dw) / 2, box.top + (ph - dh) / 2, box.left + (pw + dw) / 2, box.top + (ph + dh) / 2);
        if (m_bitmap)
            g_rt->DrawBitmap(m_bitmap.Get(), m_previewDst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    else
        Text(L"بانتظار الكاميرا…", g_fCenter.Get(), box, Theme::SubText);
    AddHit(box, ID_PREVIEW);
    Stroke(box, Theme::Border, 0);
    y += ph + 12;

    // ---- لون المفتاح ----
    Fill(Rect(rgt - 34, y + 2, rgt, y + 32), D2D1::ColorF(l.chromaColor), 7);
    Stroke(Rect(rgt - 34, y + 2, rgt, y + 32), Theme::Border, 7);
    Text(L"لون الخلفية", g_fBody.Get(), Rect(rgt - 160, y, rgt - 42, y + 34), Theme::Text);
    DrawButton(Rect(lft, y, lft + 110, y + 34), L"تلقائي", ID_AUTO, true, true);
    DrawButton(Rect(lft + 118, y, lft + 208, y + 34), L"أخضر", ID_GREEN, false, true);
    DrawButton(Rect(lft + 216, y, lft + 306, y + 34), L"أزرق", ID_BLUE, false, true);
    y += 46;

    // ---- المنزلقات ----
    auto pct = [](float v, float mx) { return std::to_wstring((int)std::lround(v / mx * 100)) + L"%"; };
    y = SliderRow(y, lft, rgt, L"الدقّة", pct(l.chromaSim, 0.8f), l.chromaSim, 0.02f, 0.8f, 0.01f, ID_SIM,
                  [this](float v) { m_h.modify([v](SceneLayer& x) { x.chromaSim = v; }); });
    y = SliderRow(y, lft, rgt, L"نعومة الحواف", pct(l.chromaSmooth, 0.4f), l.chromaSmooth, 0.01f, 0.4f, 0.01f, ID_SMOOTH,
                  [this](float v) { m_h.modify([v](SceneLayer& x) { x.chromaSmooth = v; }); });
    y = SliderRow(y, lft, rgt, L"إزالة الانعكاس الأخضر", pct(l.chromaSpill, 1.0f), l.chromaSpill, 0.0f, 1.0f, 0.05f, ID_SPILL,
                  [this](float v) { m_h.modify([v](SceneLayer& x) { x.chromaSpill = v; }); });
    Text(L"إذا بقيت بقع من الخلفية زِد «الدقّة»، وإذا اختفى جزء منك خفّفها", g_fSmall.Get(),
         Rect(lft, y + 4, rgt, y + 22), Theme::SubText);
}

bool ChromaPanel::OnDrag(int id, D2D1_POINT_2F p)
{
    if (id != ID_PREVIEW || m_frameW <= 0)
        return false;
    if (p.x < m_previewDst.left || p.x > m_previewDst.right || p.y < m_previewDst.top || p.y > m_previewDst.bottom)
        return true;
    // القطّارة: من الصورة الأصلية (قبل إزالة الخلفية)
    SceneLayer l;
    if (!m_h.get(l))
        return true;
    l.chroma = false;
    std::shared_ptr<const MediaFrame> raw = GetLayerFrame(l);
    if (!raw || raw->width <= 0)
        return true;
    const int x = std::clamp((int)((p.x - m_previewDst.left) / (m_previewDst.right - m_previewDst.left) * raw->width), 0, raw->width - 1);
    const int y = std::clamp((int)((p.y - m_previewDst.top) / (m_previewDst.bottom - m_previewDst.top) * raw->height), 0, raw->height - 1);
    // متوسط مربّع صغير (أثبت من بكسل واحد)
    long long r = 0, g = 0, b = 0, n = 0;
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx)
        {
            const int xx = std::clamp(x + dx, 0, raw->width - 1), yy = std::clamp(y + dy, 0, raw->height - 1);
            const uint8_t* px = raw->pixels.data() + ((size_t)yy * raw->width + xx) * 4;
            b += px[0]; g += px[1]; r += px[2]; ++n;
        }
    const unsigned color = (unsigned)(((r / n) << 16) | ((g / n) << 8) | (b / n));
    m_h.modify([color](SceneLayer& x) { x.chromaColor = color; x.chroma = true; });
    return true;
}

void ChromaPanel::OnClick(int id)
{
    SceneLayer l;
    if (!m_h.get(l))
        return;
    switch (id)
    {
    case ID_TOGGLE:
        m_h.modify([](SceneLayer& x) { x.chroma = !x.chroma; });
        break;
    case ID_AUTO:
    {
        l.chroma = false;
        std::shared_ptr<const MediaFrame> raw = GetLayerFrame(l);
        if (raw)
        {
            const unsigned c = DetectKeyColor(*raw);
            m_h.modify([c](SceneLayer& x) { x.chromaColor = c; x.chroma = true; });
        }
        break;
    }
    case ID_GREEN: m_h.modify([](SceneLayer& x) { x.chromaColor = 0x00B140; x.chroma = true; }); break;
    case ID_BLUE:  m_h.modify([](SceneLayer& x) { x.chromaColor = 0x0047BB; x.chroma = true; }); break;
    default: return;
    }
    if (m_h.save) m_h.save();
}
