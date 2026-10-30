// =============================================================
//  Panel.cpp - النوافذ الداخلية (شوف Panel.h)
// =============================================================
#include "Panel.h"
#include <algorithm>
#include <cmath>

PanelHost g_panels;

// =============================================================
//  عناصر جاهزة
// =============================================================
float Panel::Section(const D2D1_RECT_F& card, const std::wstring& title, const std::wstring& sub,
                     bool on, int toggleId, int resetId)
{
    Fill(card, Theme::Card, 12);
    if (on)   // خط أزرق رفيع على اليمين: القسم شغّال
        Fill(Rect(card.right - 3, card.top + 14, card.right, card.top + 44), Theme::Accent, 1.5f);

    const float y = card.top;
    Text(title, g_fBody.Get(), Rect(card.left + 170, y + 10, card.right - 16, y + 32), on ? Theme::Text : Theme::SubText);
    Text(sub, g_fSmall.Get(), Rect(card.left + 170, y + 31, card.right - 16, y + 49), Theme::SubText);

    // مفتاح التشغيل
    AddHit(Rect(card.left + 10, y + 12, card.left + 58, y + 44), toggleId);
    DrawToggle(card.left + 14, y + 16, on, true);

    // زر "افتراضي"
    const D2D1_RECT_F reset = Rect(card.left + 66, y + 14, card.left + 156, y + 42);
    const bool hover = AddHit(reset, resetId);
    if (hover)
        Fill(reset, Theme::Hover, 7);
    Text(Icon::Undo, g_fIcon.Get(), Rect(reset.right - 26, reset.top, reset.right - 6, reset.bottom),
         hover ? Theme::Text : Theme::SubText);
    Text(L"افتراضي", g_fSegment.Get(), Rect(reset.left + 4, reset.top, reset.right - 26, reset.bottom),
         hover ? Theme::Text : Theme::SubText);

    Fill(Rect(card.left + 14, y + 57, card.right - 14, y + 58), Theme::Border);
    return y + 64;
}

float Panel::ValueAtX(const D2D1_RECT_F& track, float x, float minV, float maxV, float step)
{
    float f = (track.right - x) / std::max(1.0f, track.right - track.left);   // اليمين = الأصغر
    f = std::clamp(f, 0.0f, 1.0f);
    float v = minV + f * (maxV - minV);
    if (step > 0)
        v = std::round((v - minV) / step) * step + minV;
    return std::clamp(v, minV, maxV);
}

void Panel::AddSlider(const D2D1_RECT_F& hitRect, const D2D1_RECT_F& track, float value, float minV, float maxV,
                      float step, int id, std::function<void(float)> set)
{
    AddHit(hitRect, id);
    m_sliders.push_back({ id, track, value, minV, maxV, step, std::move(set) });
}

float Panel::SliderRow(float y, float left, float right, const std::wstring& label, const std::wstring& valueText,
                       float value, float minV, float maxV, float step, int id, std::function<void(float)> set, bool dim)
{
    const float rowH = 44, cy = y + rowH / 2;
    const bool  dragging = (m_activeSlider == id);

    Text(label, g_fBody.Get(), Rect(right - 130, y, right, y + rowH), dim ? Theme::SubText : Theme::Text);
    Text(valueText, g_fSmallEnd.Get(), Rect(left, y, left + 96, y + rowH),
         dragging ? Theme::Accent : (dim ? Theme::SubText : Theme::Text));

    const D2D1_RECT_F track = Rect(left + 106, cy - 3, right - 142, cy + 3);
    const float tw = track.right - track.left;
    const D2D1_RECT_F hit = Rect(track.left - 10, y + 4, track.right + 10, y + rowH - 4);
    const bool hover = Inside(hit, g_mouse);
    AddSlider(hit, track, value, minV, maxV, step, id, std::move(set));

    const float f  = (maxV > minV) ? std::clamp((value - minV) / (maxV - minV), 0.0f, 1.0f) : 0.0f;
    const float kx = track.right - tw * f;
    Fill(track, Theme::Border, 3);
    Fill(Rect(kx, track.top, track.right, track.bottom), dim ? Theme::Off : Theme::Accent, 3);
    Circle(kx, cy, (dragging || hover) ? 8.0f : 7.0f, dim ? Theme::SubText : Theme::White);
    Circle(kx, cy, 3.0f, dim ? Theme::Off : Theme::Accent);
    return y + rowH;
}

const Panel::SliderInfo* Panel::FindSlider(int id) const
{
    for (const SliderInfo& s : m_sliders)
        if (s.id == id)
            return &s;
    return nullptr;
}

// =============================================================
//  PanelHost
// =============================================================
void PanelHost::Open(std::unique_ptr<Panel> panel)
{
    m_panel   = std::move(panel);
    m_pressed = 0;
    m_dragId  = 0;
    m_scroll  = 0;
}

void PanelHost::Close()
{
    if (m_panel)
        m_panel->OnCommit();
    m_panel.reset();
    m_pressed = 0;
    m_dragId  = 0;
}

void PanelHost::Draw(float W, float H)
{
    if (!m_panel)
        return;

    // الخلفية الغامقة فوق كل البرنامج
    Fill(Rect(0, 0, W, H), D2D1::ColorF(0x000000, 0.55f));
    AddHit(Rect(0, 0, W, H), ID_PANEL_BACKDROP);

    const float headerH = 66;
    const float bodyH   = m_panel->BodyHeight();
    const float pw = std::min(m_panel->Width(), W - 48);
    const float ph = std::min(headerH + bodyH + 12, H - 48);
    const float x = std::floor((W - pw) / 2), y = std::floor((H - ph) / 2);
    const D2D1_RECT_F card = Rect(x, y, x + pw, y + ph);

    // ظل خفيف + الكرت
    Fill(Rect(x - 2, y + 4, x + pw + 2, y + ph + 8), D2D1::ColorF(0x000000, 0.35f), 18);
    Fill(card, Theme::Panel, 14);
    Stroke(card, Theme::Border, 14);
    AddHit(card, ID_PANEL_CARD);

    // ---- الرأس: أيقونة وعنوان على اليمين، وزر الإغلاق على اليسار ----
    float titleRight = card.right - 20;
    if (const wchar_t* icon = m_panel->IconGlyph())
    {
        const D2D1_RECT_F ib = Rect(card.right - 56, y + 15, card.right - 20, y + 51);
        Fill(ib, WithAlpha(Theme::Accent, 0.16f), 10);
        Text(icon, g_fIconBig.Get(), ib, Theme::Accent);
        titleRight = card.right - 66;
    }
    const std::wstring sub = m_panel->Subtitle();
    if (sub.empty())
        Text(m_panel->Title(), g_fTitle.Get(), Rect(card.left + 60, y + 14, titleRight, y + 52), Theme::Text);
    else
    {
        Text(m_panel->Title(), g_fTitle.Get(), Rect(card.left + 60, y + 10, titleRight, y + 36), Theme::Text);
        Text(sub, g_fSmall.Get(), Rect(card.left + 60, y + 36, titleRight, y + 54), Theme::SubText);
    }
    const D2D1_RECT_F close = Rect(card.left + 14, y + 15, card.left + 50, y + 51);
    if (AddHit(close, ID_PANEL_CLOSE))
        Fill(close, Theme::Red, 8);
    Text(Icon::Close, g_fIcon.Get(), close, Theme::Text);
    Fill(Rect(card.left, y + headerH - 1, card.right, y + headerH), Theme::Border);

    // ---- المحتوى (بيتمرّر إذا ما كفّى) ----
    m_body = Rect(card.left + 1, y + headerH, card.right - 1, card.bottom - 6);
    const float viewH = m_body.bottom - m_body.top;
    m_scrollMax = std::max(0.0f, bodyH + 6 - viewH);
    m_scroll    = std::clamp(m_scroll, 0.0f, m_scrollMax);

    const bool        oldClipOn = g_hitClipOn;
    const D2D1_RECT_F oldClip   = g_hitClip;
    g_rt->PushAxisAlignedClip(m_body, D2D1_ANTIALIAS_MODE_ALIASED);
    g_hitClipOn = true;
    g_hitClip   = m_body;
    m_panel->m_sliders.clear();
    m_panel->DrawBody(card.left + 20, m_body.top + 12 - m_scroll, card.right - 20);
    g_hitClipOn = oldClipOn;
    g_hitClip   = oldClip;
    g_rt->PopAxisAlignedClip();

    // شريط تمرير رفيع على اليسار
    if (m_scrollMax > 0)
    {
        const float barH = std::max(30.0f, viewH * viewH / (viewH + m_scrollMax));
        const float barY = m_body.top + (viewH - barH) * (m_scroll / m_scrollMax);
        Fill(Rect(card.left + 4, barY + 2, card.left + 8, barY + barH - 2), Theme::Off, 2);
    }
}

void PanelHost::ApplySlider(D2D1_POINT_2F p)
{
    if (const Panel::SliderInfo* s = m_panel->FindSlider(m_panel->m_activeSlider))
        s->set(Panel::ValueAtX(s->track, p.x, s->minV, s->maxV, s->step));
}

void PanelHost::OnMouseDown(D2D1_POINT_2F p)
{
    if (!m_panel)
        return;
    m_pressed = HitAt(p);
    if (m_panel->FindSlider(m_pressed))
    {
        m_panel->m_activeSlider = m_pressed;
        ApplySlider(p);
    }
    else if (m_pressed >= ID_PANEL_USER && m_panel->OnDrag(m_pressed, p))
        m_dragId = m_pressed;
}

void PanelHost::OnMouseMove(D2D1_POINT_2F p)
{
    if (m_panel && m_panel->m_activeSlider)
        ApplySlider(p);
    else if (m_panel && m_dragId)
        m_panel->OnDrag(m_dragId, p);
}

void PanelHost::OnMouseUp(D2D1_POINT_2F p)
{
    if (!m_panel)
        return;
    if (m_dragId)                    // خلص السحب الحر
    {
        m_dragId = 0;
        m_pressed = 0;
        m_panel->OnCommit();
        return;
    }
    if (m_panel->m_activeSlider)     // خلص السحب: منحفظ
    {
        m_panel->m_activeSlider = 0;
        m_pressed = 0;
        m_panel->OnCommit();
        return;
    }
    const int id = HitAt(p), pressed = m_pressed;
    m_pressed = 0;
    if (id != pressed)               // الكبسة لازم تبلّش وتخلص على نفس الشي
        return;
    if (id == ID_PANEL_BACKDROP || id == ID_PANEL_CLOSE)
        Close();
    else if (id >= ID_PANEL_USER)
    {
        m_panel->OnClick(id);
        if (m_panel && m_panel->m_closeRequested)
        {
            std::function<void()> after = std::move(m_panel->m_afterClose);
            Close();
            if (after)
                after();         // بعد ما تسكّرت (ممكن يفتح نافذة جديدة)
        }
    }
}

void PanelHost::OnWheel(D2D1_POINT_2F p, float steps)
{
    if (!m_panel)
        return;
    if (m_panel->OnWheel(p, steps))     // النافذة بتتمرّر بطريقتها (مثلاً الإعدادات: المحتوى بس)
        return;
    // إذا المحتوى بيتمرّر: العجلة للتمرير. إذا لا: فوق المنزلق بتغيّر قيمته
    if (m_scrollMax > 0)
    {
        if (Inside(m_body, p))
            m_scroll = std::clamp(m_scroll - steps * 54.0f, 0.0f, m_scrollMax);
        return;
    }
    if (const Panel::SliderInfo* s = m_panel->FindSlider(HitAt(p)))
    {
        const float step = s->step > 0 ? s->step : (s->maxV - s->minV) / 50;
        s->set(std::clamp(s->value + steps * step, s->minV, s->maxV));
        m_panel->OnCommit();
    }
}

void PanelHost::OnKey(WPARAM key)
{
    if (m_panel && m_panel->OnKeyDown(key))
        return;
    if (key == VK_ESCAPE)
        Close();
}

void PanelHost::OnChar(wchar_t c)
{
    if (m_panel && c != 27)     // Esc بينعالج بـ OnKey
        m_panel->OnChar(c);
}

bool PanelHost::WantsHandCursor(D2D1_POINT_2F p) const
{
    return HitAt(p) > ID_PANEL_CARD;
}
