// =============================================================
//  Ui.cpp - أدوات الواجهة المشتركة (شوف Ui.h)
// =============================================================
#include "Ui.h"
#include <algorithm>
#include <cmath>

// أدوات الرسم
ComPtr<IDWriteFactory>        g_dwrite;
ComPtr<ID2D1HwndRenderTarget> g_rt;
ComPtr<ID2D1SolidColorBrush>  g_brush;
ComPtr<IDWriteTextFormat>     g_fTitle, g_fSection, g_fBody, g_fSmall, g_fSmallEnd, g_fSmallLtr, g_fCenter, g_fSegment,
                              g_fTimer, g_fIcon, g_fIconBig;

// الماوس والأزرار
std::vector<Hit> g_hits;
D2D1_POINT_2F    g_mouse = { -1, -1 };
bool             g_hitClipOn = false;
D2D1_RECT_F      g_hitClip{};

// =============================================================
//  أدوات رسم صغيرة
// =============================================================
bool Inside(const D2D1_RECT_F& r, D2D1_POINT_2F p)
{
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

D2D1_RECT_F Rect(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }

void Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius)
{
    g_brush->SetColor(c);
    if (radius > 0) g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), g_brush.Get());
    else            g_rt->FillRectangle(r, g_brush.Get());
}

void Stroke(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius, float width)
{
    g_brush->SetColor(c);
    if (radius > 0) g_rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), g_brush.Get(), width);
    else            g_rt->DrawRectangle(r, g_brush.Get(), width);
}

void Circle(float cx, float cy, float radius, const D2D1_COLOR_F& c)
{
    g_brush->SetColor(c);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), g_brush.Get());
}

void Text(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c)
{
    g_brush->SetColor(c);
    g_rt->DrawTextW(s.c_str(), (UINT32)s.size(), f, r, g_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) { c.a = a; return c; }

// بيسجّل زر، وبيرجّع إذا الماوس فوقه
bool AddHit(const D2D1_RECT_F& r, int id)
{
    D2D1_RECT_F rr = r;
    if (g_hitClipOn)
    {
        rr.left   = std::max(rr.left, g_hitClip.left);
        rr.top    = std::max(rr.top, g_hitClip.top);
        rr.right  = std::min(rr.right, g_hitClip.right);
        rr.bottom = std::min(rr.bottom, g_hitClip.bottom);
        if (rr.right <= rr.left || rr.bottom <= rr.top)
            return false;
    }
    g_hits.push_back({ rr, id });
    return Inside(rr, g_mouse);
}

// خط نص: rtl = من اليمين لليسار
ComPtr<IDWriteTextFormat> MakeFormat(float size, DWRITE_FONT_WEIGHT weight, bool rtl,
                                     DWRITE_TEXT_ALIGNMENT align, const wchar_t* family)
{
    ComPtr<IDWriteTextFormat> f;
    g_dwrite->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                               size, rtl ? L"ar" : L"en-us", &f);
    f->SetReadingDirection(rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
    f->SetTextAlignment(align);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // إذا النص طويل منحط "..." بآخره
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(g_dwrite->CreateEllipsisTrimmingSign(f.Get(), &ellipsis)))
    {
        DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        f->SetTrimming(&trim, ellipsis.Get());
    }
    return f;
}

void CreateTextFormats()
{
    const auto LEAD = DWRITE_TEXT_ALIGNMENT_LEADING;    // بالعربي = يمين
    const auto MID  = DWRITE_TEXT_ALIGNMENT_CENTER;
    g_fTitle    = MakeFormat(19, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, LEAD);
    g_fSection  = MakeFormat(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, LEAD);
    g_fBody     = MakeFormat(14, DWRITE_FONT_WEIGHT_NORMAL, true, LEAD);
    g_fSmall    = MakeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, true, LEAD);
    g_fSmallEnd = MakeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, true, DWRITE_TEXT_ALIGNMENT_TRAILING);   // على اليسار
    g_fSmallLtr = MakeFormat(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fSegment  = MakeFormat(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, MID);
    g_fCenter   = MakeFormat(14, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, MID);
    g_fTimer    = MakeFormat(26, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, MID);
    g_fIcon     = MakeFormat(16, DWRITE_FONT_WEIGHT_NORMAL, false, MID, L"Segoe MDL2 Assets");
    g_fIconBig  = MakeFormat(20, DWRITE_FONT_WEIGHT_NORMAL, false, MID, L"Segoe MDL2 Assets");
}

// عرض نص بالنقاط (لحتى نعرف قدّيش نعمل الزر عريض)
float TextWidth(const std::wstring& s, IDWriteTextFormat* f)
{
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g_dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 2000, 50, &layout)))
        return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

// ---------- مفتاح تشغيل/إطفاء ----------
void DrawToggle(float x, float y, bool on, bool enabled)
{
    // وقت التسجيل المفتاح مقفول، بس منخلّي لونه واضح (أزرق = شغّال ، رمادي = مطفي)
    // لحتى تعرف حالته بنظرة وحدة
    const D2D1_RECT_F track = Rect(x, y, x + 42, y + 24);
    Fill(track, on ? Theme::Accent : (enabled ? Theme::Off : WithAlpha(Theme::Off, 0.6f)), 12);
    Circle(on ? x + 30 : x + 12, y + 12, 8, D2D1::ColorF(0xFFFFFF, (on || enabled) ? 1.0f : 0.6f));
}

// ---------- شريط مستوى الصوت ----------
void DrawMeter(const D2D1_RECT_F& r, float level, bool active)
{
    Fill(r, Theme::Border, 3);
    if (!active || level <= 0)
        return;
    const D2D1_COLOR_F c = level < 0.7f ? Theme::Green : (level < 0.9f ? Theme::Yellow : Theme::Red);
    const float w = (r.right - r.left) * level;
    Fill(Rect(r.right - w, r.top, r.right, r.bottom), c, 3);
}

// ---------- أزرار متلاصقة (نختار واحد منها) ----------
void DrawSegmented(const D2D1_RECT_F& r, const std::vector<std::wstring>& labels, int selected, int firstId, bool enabled)
{
    Fill(r, Theme::Card, 9);
    const float w = (r.right - r.left) / labels.size();
    for (size_t i = 0; i < labels.size(); ++i)
    {
        const float right = r.right - i * w;               // الخيار الأول على اليمين
        const D2D1_RECT_F seg = Rect(right - w + 3, r.top + 3, right - 3, r.bottom - 3);
        const bool hover = AddHit(seg, firstId + (int)i) && enabled;
        const bool sel   = ((int)i == selected);
        if (sel)        Fill(seg, enabled ? Theme::Accent : Theme::Off, 7);
        else if (hover) Fill(seg, Theme::Hover, 7);
        Text(labels[i], g_fSegment.Get(), seg, sel ? Theme::White : (enabled ? Theme::Text : Theme::SubText));
    }
}

int HitAt(D2D1_POINT_2F p)
{
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
        if (Inside(it->rect, p))
            return it->id;
    return 0;
}


// =============================================================
//  عناصر جديدة
// =============================================================
void Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float width)
{
    g_brush->SetColor(c);
    g_rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), g_brush.Get(), width);
}

bool DrawButton(const D2D1_RECT_F& r, const std::wstring& label, int id, bool primary, bool enabled, const wchar_t* icon)
{
    const bool hover = AddHit(r, id) && enabled;
    if (primary)
        Fill(r, !enabled ? Theme::Off : (hover ? D2D1::ColorF(0x6A9DFF) : Theme::Accent), 8);
    else
    {
        Fill(r, hover ? Theme::Hover : Theme::Card, 8);
        Stroke(r, Theme::Border, 8);
    }
    const D2D1_COLOR_F color = primary ? Theme::White : (enabled ? Theme::Text : Theme::SubText);
    if (icon)
    {
        // الأيقونة على يمين النص (لأن الواجهة من اليمين لليسار)
        const float tw = TextWidth(label, g_fSegment.Get());
        const float total = tw + 24;
        const float cx = (r.left + r.right) / 2;
        const float right = cx + total / 2;
        Text(icon, g_fIcon.Get(), Rect(right - 18, r.top, right, r.bottom), color);
        Text(label, g_fSegment.Get(), Rect(right - total - 4, r.top, right - 22, r.bottom), color);
    }
    else
        Text(label, g_fSegment.Get(), r, color);
    return hover;
}

float PeakToMeter(float peak)
{
    if (peak <= 0.00001f)
        return 0.0f;
    const float db = 20.0f * std::log10(peak);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

std::wstring LtrText(const std::wstring& s)
{
    const wchar_t lre = 0x202A, pdf = 0x202C;   // بداية ونهاية نص من اليسار لليمين
    return std::wstring(1, lre) + s + std::wstring(1, pdf);
}

// =============================================================
//  الثيمات
// =============================================================
namespace Theme
{
    namespace
    {
        struct Palette { UINT32 bg, panel, card, hover, border, text, sub, off; };
        const Palette PALETTES[THEME_COUNT] = {
            { 0x0E1014, 0x16191F, 0x1D2129, 0x252A34, 0x2A2F3A, 0xE8EAED, 0x8B93A3, 0x3A404D },   // داكن
            { 0x000000, 0x0B0C0E, 0x131418, 0x1C1E23, 0x22252B, 0xEDEEF0, 0x8A909C, 0x34383F },   // أسود (لشاشات OLED)
            { 0x0D1321, 0x141C2E, 0x1A2338, 0x222D47, 0x27324D, 0xE6EBF5, 0x8D99B3, 0x37425E },   // كحلي
        };
        const UINT32 ACCENTS[ACCENT_COUNT] = { 0x4F8CFF, 0x8B5CF6, 0x14B8A6, 0xF97316, 0xEC4899, 0xEAB308 };
        const wchar_t* THEME_NAMES[THEME_COUNT]   = { L"داكن", L"أسود", L"كحلي" };
        const wchar_t* ACCENT_NAMES[ACCENT_COUNT] = { L"أزرق", L"بنفسجي", L"فيروزي", L"برتقالي", L"وردي", L"ذهبي" };
    }

    const wchar_t* ThemeName(int i)    { return THEME_NAMES[(i % THEME_COUNT + THEME_COUNT) % THEME_COUNT]; }
    const wchar_t* AccentName(int i)   { return ACCENT_NAMES[(i % ACCENT_COUNT + ACCENT_COUNT) % ACCENT_COUNT]; }
    D2D1_COLOR_F   AccentPreview(int i){ return D2D1::ColorF(ACCENTS[(i % ACCENT_COUNT + ACCENT_COUNT) % ACCENT_COUNT]); }

    void Apply(int theme, int accent)
    {
        const Palette& p = PALETTES[(theme % THEME_COUNT + THEME_COUNT) % THEME_COUNT];
        Background = D2D1::ColorF(p.bg);
        Panel      = D2D1::ColorF(p.panel);
        Card       = D2D1::ColorF(p.card);
        Hover      = D2D1::ColorF(p.hover);
        Border     = D2D1::ColorF(p.border);
        Text       = D2D1::ColorF(p.text);
        SubText    = D2D1::ColorF(p.sub);
        Off        = D2D1::ColorF(p.off);
        Accent     = AccentPreview(accent);
    }
}
