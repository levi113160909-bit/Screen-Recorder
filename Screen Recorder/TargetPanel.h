// =============================================================
//  TargetPanel.h - نافذة "إعدادات منصة البث" (يوتيوب، تويتش...)
//
//  لكل منصة لحالها:
//   - الترميز: H.264 (كل المنصات) أو HEVC (يوتيوب: أوضح بمعدّل بت أقل)
//   - معدّل البت: الإعداد العام للبث أو رقم خاص فيها
//  المنصات اللي إلها نفس الإعدادات بتتشارك نفس الفيديو المضغوط
//  (كرت الشاشة بيضغط مرة وحدة بس).
// =============================================================
#pragma once
#include "Panel.h"
#include "Rtmp.h"

struct TargetPanelHooks
{
    std::function<StreamTarget*()> target;          // المنصة (ممكن تختفي إذا انحذفت)
    std::function<UINT32()>        defaultKbps;     // معدّل البت العام للبث
    // دقة البث وإطاراته لهالمنصة (العمودي غير الأفقي)
    std::function<void(UINT32& w, UINT32& h, UINT32& fps)> format;
    std::function<std::wstring()>  sharedWith;      // أسماء المنصات اللي بتتشارك معها الضغط
    std::function<void()>          changed;         // حفظ
};

// معدّل البت اللي بينصح فيه يوتيوب (كيلوبت/ث) حسب الدقة والإطارات والترميز
UINT32 YouTubeRecommendedKbps(UINT32 w, UINT32 h, UINT32 fps, bool hevc);

class TargetPanel : public Panel
{
public:
    explicit TargetPanel(TargetPanelHooks hooks);

    std::wstring   Title() const override;
    std::wstring   Subtitle() const override    { return L"الترميز ومعدّل البت لهذه المنصة فقط"; }
    const wchar_t* IconGlyph() const override   { return Icon::Settings; }
    float          Width() const override       { return 600; }
    float          BodyHeight() const override  { return m_height; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;

private:
    TargetPanelHooks m_h;
    float            m_height = 420;
};
