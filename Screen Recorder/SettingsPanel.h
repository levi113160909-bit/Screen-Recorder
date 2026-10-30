// =============================================================
//  SettingsPanel.h - نافذة "الإعدادات" (كل الإعدادات بمكان واحد)
//
//  على اليمين قائمة أقسام، وعلى اليسار محتوى القسم (بيتمرّر لحالو).
//  مستوى واحد بس: ما في نافذة جوّا نافذة.
//
//  الأقسام:
//   - التسجيل، البث، المؤشر والإشعارات، عام: بيرسمهم main.cpp
//     (لأنهم مربوطين بإعدادات البرنامج مباشرة)
//   - الإعادة والاختصارات والمعرض: صفحات جاهزة (ReplayPanel و HotkeysPanel و GalleryPanel)
// =============================================================
#pragma once
#include "Panel.h"

enum SettingsPage : int
{
    SP_RECORDING = 0, SP_STREAM, SP_REPLAY, SP_HOTKEYS, SP_OVERLAY, SP_GENERAL,
    SP_GALLERY,          // بآخر الترقيم (لحتى ما تتغيّر أرقام القديم)، بس بينعرض أول وحدة بالقائمة
    SP_COUNT
};

// أرقام عناصر main.cpp جوّا نافذة الإعدادات = الرقم الأصلي + هالرقم
// (لحتى توصل للنافذة، وهي بترجّعها لـ main.cpp بالرقم الأصلي)
constexpr int SETTINGS_ID_OFFSET = 60000;
static_assert(SETTINGS_ID_OFFSET > ID_PANEL_LAST, "أرقام الإعدادات لازم تكون بعد أرقام النوافذ");

struct SettingsHooks
{
    // رسم صفحة من main.cpp: بترجّع طول المحتوى
    std::function<float(int page, float left, float top, float right)> drawPage;
    // كبسة على عنصر من main.cpp (بالرقم الأصلي)
    std::function<void(int id)> click;
};

class SettingsPanel : public Panel
{
public:
    SettingsPanel(SettingsHooks hooks, std::unique_ptr<Panel> replay, std::unique_ptr<Panel> hotkeys,
                  std::unique_ptr<Panel> gallery, int page);

    std::wstring   Title() const override    { return L"الإعدادات"; }
    const wchar_t* IconGlyph() const override { return Icon::Settings; }
    float          Width() const override    { return 880; }
    float          BodyHeight() const override { return 590; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    bool           OnKeyDown(WPARAM vk) override;
    bool           CapturingKeys() const override;
    bool           OnWheel(D2D1_POINT_2F p, float steps) override;

    int Page() const { return m_page; }

private:
    Panel* Embedded() const;           // الصفحة الجاهزة الحالية (أو فاضي)

    SettingsHooks          m_hooks;
    std::unique_ptr<Panel> m_replay, m_hotkeys, m_gallery;
    int                    m_page = SP_RECORDING;
    float                  m_scroll = 0, m_scrollMax = 0;
    D2D1_RECT_F            m_content{};
};
