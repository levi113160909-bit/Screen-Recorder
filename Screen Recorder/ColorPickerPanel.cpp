// =============================================================
//  ColorPickerPanel.cpp - نافذة "اختيار لون"
// =============================================================
#include "ColorPickerPanel.h"
#include <algorithm>
#include <cmath>

namespace
{
    enum : int
    {
        ID_WHEEL  = ID_PANEL_USER,
        ID_VALUE  = ID_PANEL_USER + 1,
        ID_OK     = ID_PANEL_USER + 2,
        ID_CANCEL = ID_PANEL_USER + 3,
        ID_SWATCH = ID_PANEL_USER + 10      // ألوان سريعة +0..7
    };
    constexpr float PI = 3.14159265f;
    constexpr int   WHEEL_PX = 256;
    const UINT32 QUICK[] = { 0xEF4444, 0xF97316, 0xEAB308, 0x22C55E, 0x06B6D4, 0x3B82F6, 0x8B5CF6, 0xEC4899 };

    void HsvToRgb(float h, float s, float v, float& r, float& g, float& b)
    {
        h = std::fmod(h, 1.0f) * 6.0f;
        if (h < 0) h += 6.0f;
        const int   i = (int)h;
        const float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch (i % 6)
        {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
        }
    }
    UINT32 Pack(float r, float g, float b)
    {
        auto c = [](float x) { return (UINT32)std::lround(std::clamp(x, 0.0f, 1.0f) * 255); };
        return (c(r) << 16) | (c(g) << 8) | c(b);
    }
}

ColorPickerPanel::ColorPickerPanel(UINT32 initial, std::function<void(UINT32)> changed, std::function<void(UINT32, bool)> done)
    : m_initial(initial), m_changed(std::move(changed)), m_done(std::move(done))
{
    SetFromRgb(initial);
}

void ColorPickerPanel::SetFromRgb(UINT32 rgb)
{
    const float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
    const float mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
    m_v = mx;
    m_s = mx > 0 ? d / mx : 0;
    float h = 0;
    if (d > 0)
    {
        if (mx == r)      h = std::fmod((g - b) / d, 6.0f);
        else if (mx == g) h = (b - r) / d + 2;
        else              h = (r - g) / d + 4;
        h /= 6.0f;
        if (h < 0) h += 1;
    }
    m_h = h;
}

UINT32 ColorPickerPanel::Current() const
{
    float r, g, b;
    HsvToRgb(m_h, m_s, m_v, r, g, b);
    return Pack(r, g, b);
}

// صورة الدائرة (بسطوع كامل): منرسمها مرة وحدة
void ColorPickerPanel::EnsureWheel()
{
    if (m_wheel && m_wheelOwner == g_rt.Get())
        return;
    m_wheel.Reset();
    m_wheelOwner = g_rt.Get();
    if (!g_rt)
        return;
    std::vector<UINT32> px((size_t)WHEEL_PX * WHEEL_PX, 0);
    const float c = WHEEL_PX / 2.0f, R = c - 1;
    for (int y = 0; y < WHEEL_PX; ++y)
        for (int x = 0; x < WHEEL_PX; ++x)
        {
            const float dx = x + 0.5f - c, dy = y + 0.5f - c;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > R + 1)
                continue;
            float hue = std::atan2(-dy, dx) / (2 * PI);
            if (hue < 0) hue += 1;
            float r, g, b;
            HsvToRgb(hue, std::min(1.0f, dist / R), 1.0f, r, g, b);
            const float a = std::clamp(R + 1 - dist, 0.0f, 1.0f);       // حافة ناعمة
            auto q = [&](float v) { return (UINT32)std::lround(v * a * 255); };
            px[(size_t)y * WHEEL_PX + x] = ((UINT32)std::lround(a * 255) << 24) | (q(r) << 16) | (q(g) << 8) | q(b);
        }
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_rt->CreateBitmap(D2D1::SizeU(WHEEL_PX, WHEEL_PX), px.data(), WHEEL_PX * 4, props, &m_wheel);
}

void ColorPickerPanel::DrawBody(float L, float top, float R)
{
    EnsureWheel();
    const float size = 240;
    const float cx = (L + R) / 2, cy = top + 12 + size / 2;
    m_center = D2D1::Point2F(cx, cy);
    m_radius = size / 2;
    const D2D1_RECT_F wr = Rect(cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2);
    AddHit(wr, ID_WHEEL);
    if (m_wheel)
        g_rt->DrawBitmap(m_wheel.Get(), wr, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    // السطوع: طبقة سودا فوق الدائرة
    if (m_v < 1)
    {
        g_brush->SetColor(D2D1::ColorF(0, 0, 0, 1 - m_v));
        g_rt->FillEllipse(D2D1::Ellipse(m_center, m_radius, m_radius), g_brush.Get());
    }
    // المؤشر
    const float ang = m_h * 2 * PI;
    const D2D1_POINT_2F k = D2D1::Point2F(cx + std::cos(ang) * m_s * m_radius, cy - std::sin(ang) * m_s * m_radius);
    g_brush->SetColor(D2D1::ColorF(0x000000, 0.6f));
    g_rt->DrawEllipse(D2D1::Ellipse(k, 9, 9), g_brush.Get(), 3);
    g_brush->SetColor(D2D1::ColorF(0xFFFFFF));
    g_rt->DrawEllipse(D2D1::Ellipse(k, 9, 9), g_brush.Get(), 2);

    float y = cy + size / 2 + 18;
    const float l = L + 16, r = R - 16;

    // منزلق السطوع (الأفتح على اليسار متل باقي المنزلقات)
    Text(L"السطوع", g_fSmall.Get(), Rect(r - 60, y, r, y + 20), Theme::SubText);
    const D2D1_RECT_F track = Rect(l, y + 8, r - 70, y + 12);
    Fill(track, Theme::Border, 2);
    const float kx = track.right - (track.right - track.left) * m_v;
    Fill(Rect(kx, track.top, track.right, track.bottom), Theme::SubText, 2);
    Circle(kx, y + 10, 8, Theme::White);
    AddSlider(Rect(track.left - 8, y - 4, track.right + 8, y + 24), track, m_v, 0.0f, 1.0f, 0.01f, ID_VALUE,
              [this](float v) { m_v = v; if (m_changed) m_changed(Current()); });
    y += 34;

    // ألوان سريعة + المعاينة
    const float sw = 28, gap = 8;
    for (int i = 0; i < 8; ++i)
    {
        const float x0 = r - (i + 1) * (sw + gap) + gap;
        const D2D1_RECT_F s = Rect(x0, y, x0 + sw, y + sw);
        AddHit(s, ID_SWATCH + i);
        Fill(s, D2D1::ColorF(QUICK[i]), 7);
    }
    const D2D1_RECT_F prev = Rect(l, y - 2, l + 92, y + sw + 2);
    Fill(prev, D2D1::ColorF(Current()), 8);
    Stroke(prev, Theme::Border, 8);
    y += sw + 18;

    DrawButton(Rect(r - 120, y, r, y + 34), L"تم", ID_OK, true, true);
    DrawButton(Rect(r - 250, y, r - 130, y + 34), L"إلغاء", ID_CANCEL, false, true);
}

bool ColorPickerPanel::OnDrag(int id, D2D1_POINT_2F p)
{
    if (id != ID_WHEEL)
        return false;
    const float dx = p.x - m_center.x, dy = p.y - m_center.y;
    float hue = std::atan2(-dy, dx) / (2 * PI);
    if (hue < 0) hue += 1;
    m_h = hue;
    m_s = std::clamp(std::sqrt(dx * dx + dy * dy) / std::max(1.0f, m_radius), 0.0f, 1.0f);
    if (m_v < 0.15f) m_v = 1.0f;                 // كان أسود تقريباً: منرجّع السطوع لحتى يبيّن اللون
    if (m_changed) m_changed(Current());
    return true;
}

void ColorPickerPanel::OnClick(int id)
{
    if (id >= ID_SWATCH && id < ID_SWATCH + 8)
    {
        SetFromRgb(QUICK[id - ID_SWATCH]);
        if (m_changed) m_changed(Current());
    }
    else if (id == ID_OK || id == ID_CANCEL)
    {
        // بعد ما تتسكّر النافذة (لأنو done ممكن يفتح نافذة الإعدادات من جديد)
        m_finished = true;
        const bool ok = (id == ID_OK);
        const UINT32 color = ok ? Current() : m_initial;
        auto done = m_done;
        CloseThen([done, color, ok] { if (done) done(color, ok); });
    }
}

// انسكّرت بلا "تم" (Esc أو كبسة برّا النافذة): منرجّع اللون القديم
ColorPickerPanel::~ColorPickerPanel()
{
    if (!m_finished && m_changed)
        m_changed(m_initial);
}
