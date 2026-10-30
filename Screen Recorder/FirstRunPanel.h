// =============================================================
//  FirstRunPanel.h - نافذة "أهلاً بك" (أول مرة بتفتح البرنامج)
//
//  سؤال واحد: شو بدك تعمل؟ (لقطات / تسجيل / بث - فيك تختار أكتر من وحدة)
//  إذا اخترت اللقطات: كم ثانية بدك تحفظ؟ وأي زر؟
//  وبالآخر ملخّص بكلام بسيط لشو انضبط. بلا "معدّل بت" ولا مصطلحات.
//  البرنامج بيكشف كرت الشاشة والشاشة لحاله وبيختار الأنسب.
// =============================================================
#pragma once
#include "Panel.h"
#include <memory>

struct SetupChoice
{
    bool clips = true, record = false, stream = false;
    int  clipSeconds = 0;          // 0: 15 ، 1: 30 ، 2: 60 ، 3: 120
};

struct FirstRunHooks
{
    std::function<std::wstring()>                    saveKey;      // زر حفظ اللقطة الحالي (نص)
    std::function<void(std::function<void()>)>       changeKey;    // التقاط زر جديد ، والدالة بتنادى لما يخلص
    std::function<std::vector<std::wstring>(const SetupChoice&)> summary;   // شو رح ينضبط (أسطر)
    std::function<void(const SetupChoice&)>          apply;        // طبّق
    std::function<void()>                            skip;         // تخطّى (بلا تغيير)
};

class FirstRunPanel : public Panel
{
public:
    explicit FirstRunPanel(FirstRunHooks hooks) : m_h(std::move(hooks)) {}
    ~FirstRunPanel() override { *m_alive = false; }

    std::wstring   Title() const override    { return L"أهلًا بك في مسجّل الشاشة"; }
    std::wstring   Subtitle() const override { return L"إعداد سريع في أقل من دقيقة"; }
    const wchar_t* IconGlyph() const override { return L""; }
    float          Width() const override    { return 760; }
    float          BodyHeight() const override { return 500; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;

private:
    FirstRunHooks m_h;
    SetupChoice   m_c;
    int           m_step = 0;            // 0 الاختيار ، 1 اللقطات ، 2 الملخّص
    bool          m_capturing = false;   // عم نستنى زر جديد
    bool          m_finished = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);   // للنداء المتأخر (التقاط الزر)
};
