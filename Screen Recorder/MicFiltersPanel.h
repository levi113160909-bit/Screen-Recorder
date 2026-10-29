// =============================================================
//  MicFiltersPanel.h - نافذة فلاتر الميكروفون
//
//  فيها: مقياس صوت حي (مع خط حد البوابة)، وإعدادات كل فلتر:
//  إزالة الضجيج، البوابة، توحيد العلو. كل تغيير بيشتغل فوراً.
// =============================================================
#pragma once
#include "Panel.h"
#include "Audio.h"

class MicFiltersPanel : public Panel
{
public:
    // active: الميكروفون شغّال هلق؟   save: حفظ الإعدادات
    MicFiltersPanel(AudioSource& mic, std::function<bool()> active, std::function<void()> save);

    std::wstring   Title() const override    { return L"فلاتر الميكروفون"; }
    std::wstring   Subtitle() const override { return L"تُطبَّق التغييرات فورًا وتُحفظ تلقائيًا"; }
    const wchar_t* IconGlyph() const override { return Icon::Filter; }
    float          Width() const override    { return 540; }
    float          BodyHeight() const override;
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    void           OnCommit() override { m_save(); }

private:
    float DrawMeters(float y, float L, float R);

    AudioSource&          m_mic;
    MicFilters&           m_f;
    std::function<bool()> m_active;
    std::function<void()> m_save;
    float m_rawLevel = 0, m_inLevel = 0, m_outLevel = 0;      // المقاييس (بتنزل بنعومة)
    float m_shownRaw = 0, m_shownIn = 0, m_shownOut = 0;       // الأرقام المعروضة
    int   m_numberTick = 0;
    int   m_learnFlash = 0;                   // بعد "تعلّم الضجيج": منبيّن رسالة كم لحظة
};
