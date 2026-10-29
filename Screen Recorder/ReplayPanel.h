// =============================================================
//  ReplayPanel.h - نافذة "إعدادات الإعادة"
//
//  الإعادة = البرنامج عم يسجّل بالخلفية دايماً بالذاكرة، ولما تكبس
//  اختصار الحفظ بيحفظ آخر كم ثانية بملف. هون بتختار:
//  شو بيتسجّل (الشاشة كاملة أو المشهد)، الصوت، المدة، الدقة،
//  الإطارات، الجودة، ووين بينحفظ.
// =============================================================
#pragma once
#include "Panel.h"

// إعدادات الإعادة (main.cpp بيحفظهم بـ settings.ini)
struct ReplaySettings
{
    int  source  = 0;      // 0 الشاشة كاملة (الافتراضي) ، 1 المشهد الحالي
    int  monitor = 0;      // أي شاشة (0 = الرئيسية)
    int  seconds = 1;      // 0: 15 ، 1: 30 ، 2: 60 ، 3: 120
    int  res     = 1;      // 0 الأصلية ، 1 1080p ، 2 720p
    int  fps     = 1;      // 0: 30 ، 1: 60
    int  quality = 1;      // 0 منخفضة ، 1 متوسطة ، 2 عالية
    bool system  = true;   // صوت النظام (كل أصوات الكمبيوتر)
    bool mic     = true;   // الميكروفون (إذا مفعّل بالمصادر)
    bool autoStart = false;   // تشغيل تلقائي لما ينفتح البرنامج
    std::wstring folder;   // فاضي = "Replays" جوّا مجلد التسجيلات
};

// معلومات من main.cpp للنافذة
struct ReplayPanelHooks
{
    std::function<void(bool restart)>          changed;     // حفظ (وإعادة تشغيل الإعادة إذا شغّالة)
    std::function<bool()>                      pickFolder;  // اختيار مجلد (true = تغيّر)
    std::function<void()>                      openFolder;
    std::function<std::wstring()>              folderPath;  // المجلد الفعلي
    std::function<std::vector<std::wstring>()> monitors;    // أسماء الشاشات (الرئيسية أول)
    std::function<bool()>                      micEnabled;  // المايك مفعّل بالمصادر؟
    std::function<int()>                       sourceW;     // عرض المصدر الأصلي (للحسابات)
    std::function<int()>                       sourceH;
};

// معدّل البت للإعادة (كيلوبت/ث) حسب الدقة والإطارات والجودة
UINT32 ReplayKbps(const ReplaySettings& s, int w, int h);

class ReplayPanel : public Panel
{
public:
    ReplayPanel(ReplaySettings& settings, ReplayPanelHooks hooks);

    std::wstring   Title() const override    { return L"إعدادات الإعادة"; }
    std::wstring   Subtitle() const override { return L"تسجيل مستمر في الخلفية، واحفظ آخر لحظات بضغطة زر"; }
    const wchar_t* IconGlyph() const override { return Icon::Replay; }
    float          Width() const override    { return 580; }
    float          BodyHeight() const override { return m_height; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;

private:
    ReplaySettings&  m_s;
    ReplayPanelHooks m_h;
    float            m_height = 600;
};
