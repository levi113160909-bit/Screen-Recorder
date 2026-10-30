// =============================================================
//  SettingsPanel.cpp - نافذة "الإعدادات"
// =============================================================
#include "SettingsPanel.h"
#include <algorithm>

namespace
{
    constexpr int   ID_NAV_FIRST = ID_PANEL_SETTINGS_NAV;
    static_assert(ID_NAV_FIRST + SP_COUNT <= ID_PANEL_SETTINGS_NAV + 100, "قائمة الإعدادات أكبر من الأرقام المحجوزة إلها");
    constexpr float NAV_W = 200;

    struct PageInfo { wchar_t glyph; const wchar_t* name; const wchar_t* desc; };
    const PageInfo PAGES[SP_COUNT] = {
        { 0xE7C8, L"التسجيل",            L"الدقة والإطارات والجودة والترميز ومكان الحفظ" },
        { 0xE93E, L"البث",               L"دقة البث وإطاراته ومعدّل البت (منفصلة عن التسجيل)" },
        { 0xE81C, L"الإعادة",            L"تسجيل مستمر في الخلفية، واحفظ آخر لحظات بضغطة زر" },
        { 0xE765, L"الاختصارات",         L"تعمل حتى داخل الألعاب وأنت خارج البرنامج" },
        { 0xEA8F, L"المؤشر والإشعارات", L"ما يظهر على الشاشة أثناء التسجيل (ولا يظهر في الفيديو)" },
        { 0xE771, L"عام",                L"الإغلاق، التشغيل مع ويندوز، والانتقال بين المشاهد" },
        { 0xE8B9, L"المعرض",             L"تسجيلاتك وإعاداتك وبثوثك وصورك: شغّل، قصّ، أعد التسمية أو احذف" },
    };
    // ترتيب القائمة على الشاشة (المعرض أول وحدة، وتحته خط فاصل)
    const int ORDER[SP_COUNT] = { SP_GALLERY, SP_RECORDING, SP_STREAM, SP_REPLAY, SP_HOTKEYS, SP_OVERLAY, SP_GENERAL };
}

SettingsPanel::SettingsPanel(SettingsHooks hooks, std::unique_ptr<Panel> replay, std::unique_ptr<Panel> hotkeys,
                             std::unique_ptr<Panel> gallery, int page)
    : m_hooks(std::move(hooks)), m_replay(std::move(replay)), m_hotkeys(std::move(hotkeys)), m_gallery(std::move(gallery)),
      m_page(std::clamp(page, 0, SP_COUNT - 1))
{
}

Panel* SettingsPanel::Embedded() const
{
    if (m_page == SP_REPLAY)  return m_replay.get();
    if (m_page == SP_HOTKEYS) return m_hotkeys.get();
    if (m_page == SP_GALLERY) return m_gallery.get();
    return nullptr;
}

void SettingsPanel::DrawBody(float L, float top, float R)
{
    const float bottom = top + BodyHeight() - 12;

    // ---- القائمة (يمين) ----
    const float navL = R - NAV_W;
    float y = top;
    for (int k = 0; k < SP_COUNT; ++k)
    {
        const int i = ORDER[k];
        const D2D1_RECT_F row = Rect(navL, y, R, y + 46);
        const bool sel = (i == m_page);
        const bool hover = AddHit(row, ID_NAV_FIRST + i);
        if (sel)        Fill(row, WithAlpha(Theme::Accent, 0.16f), 10);
        else if (hover) Fill(row, Theme::Hover, 10);
        if (sel)
            Fill(Rect(row.right - 3, row.top + 10, row.right, row.bottom - 10), Theme::Accent, 1.5f);
        const wchar_t g[] = { PAGES[i].glyph, 0 };
        Text(g, g_fIcon.Get(), Rect(row.right - 44, row.top, row.right - 14, row.bottom), sel ? Theme::Accent : Theme::SubText);
        Text(PAGES[i].name, g_fBody.Get(), Rect(row.left + 8, row.top, row.right - 50, row.bottom),
             sel ? Theme::Text : Theme::SubText);
        y += 50;
        if (i == SP_GALLERY)            // خط فاصل: المعرض مش إعداد
        {
            Fill(Rect(navL + 10, y + 2, R - 10, y + 3), Theme::Border);
            y += 10;
        }
    }
    // خط فاصل
    Fill(Rect(navL - 14, top, navL - 13, bottom), Theme::Border);

    // ---- المحتوى (يسار) ----
    const float cL = L, cR = navL - 28;
    Text(PAGES[m_page].name, g_fTitle.Get(), Rect(cL, top, cR, top + 30), Theme::Text);
    Text(PAGES[m_page].desc, g_fSmall.Get(), Rect(cL, top + 30, cR, top + 50), Theme::SubText);
    m_content = Rect(cL - 4, top + 60, cR + 4, bottom);

    const bool        oldClipOn = g_hitClipOn;
    const D2D1_RECT_F oldClip   = g_hitClip;
    D2D1_RECT_F clip = m_content;
    if (oldClipOn)
    {
        clip.left = std::max(clip.left, oldClip.left);   clip.top = std::max(clip.top, oldClip.top);
        clip.right = std::min(clip.right, oldClip.right); clip.bottom = std::min(clip.bottom, oldClip.bottom);
    }
    g_rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    g_hitClipOn = true;
    g_hitClip   = clip;

    const float viewH = m_content.bottom - m_content.top;
    m_scroll = std::clamp(m_scroll, 0.0f, m_scrollMax);
    const float y0 = m_content.top + 4 - m_scroll;
    float h = 0;
    if (Panel* e = Embedded())
    {
        e->DrawBody(cL, y0, cR);
        h = e->BodyHeight();
    }
    else if (m_hooks.drawPage)
        h = m_hooks.drawPage(m_page, cL, y0, cR);
    m_scrollMax = std::max(0.0f, h + 8 - viewH);

    g_hitClipOn = oldClipOn;
    g_hitClip   = oldClip;
    g_rt->PopAxisAlignedClip();

    // شريط تمرير رفيع
    if (m_scrollMax > 0)
    {
        const float barH = std::max(30.0f, viewH * viewH / (viewH + m_scrollMax));
        const float barY = m_content.top + (viewH - barH) * (m_scroll / m_scrollMax);
        Fill(Rect(m_content.left - 6, barY, m_content.left - 2, barY + barH), Theme::Off, 2);
    }
}

void SettingsPanel::OnClick(int id)
{
    if (id >= ID_NAV_FIRST && id < ID_NAV_FIRST + SP_COUNT)
    {
        if (CapturingKeys())
            m_hotkeys->OnKeyDown(VK_ESCAPE);       // كان عم يستنى اختصار: منلغي
        m_page   = id - ID_NAV_FIRST;
        m_scroll = 0;
        if (Panel* e = Embedded())
            e->OnShow();
        return;
    }
    if (id >= SETTINGS_ID_OFFSET)
    {
        if (m_hooks.click)
            m_hooks.click(id - SETTINGS_ID_OFFSET);
        return;
    }
    if (Panel* e = Embedded())
    {
        e->OnClick(id);
        AdoptClose(*e);             // مثلاً المعرض فتح نافذة القص
    }
}

bool SettingsPanel::OnKeyDown(WPARAM vk)
{
    Panel* e = Embedded();
    return e && e->OnKeyDown(vk);
}

bool SettingsPanel::CapturingKeys() const
{
    const Panel* e = Embedded();
    return e && e->CapturingKeys();
}

bool SettingsPanel::OnWheel(D2D1_POINT_2F p, float steps)
{
    if (!Inside(m_content, p))
        return true;               // القائمة ما بتتمرّر
    m_scroll = std::clamp(m_scroll - steps * 54.0f, 0.0f, m_scrollMax);
    return true;
}
