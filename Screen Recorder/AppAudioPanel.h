// =============================================================
//  AppAudioPanel.h - نافذة "إضافة صوت برنامج"
//
//  بتعرض البرامج المفتوحة بأيقوناتها الحقيقية:
//   - اللي عم يطلّعوا صوت هلق فوق (مع مؤشر صوت متحرك)
//   - وبعدهن باقي البرامج المفتوحة
//  كبسة على برنامج = بينضاف صوته للمشهد وبتتسكّر النافذة
// =============================================================
#pragma once
#include "Panel.h"
#include "AppAudio.h"
#include "IconCache.h"

class AppAudioPanel : public Panel
{
public:
    // isAdded: هالبرنامج مضاف للمشهد من قبل؟   add: ضيفه
    AppAudioPanel(std::function<bool(const std::wstring&)> isAdded, std::function<void(const AudioApp&)> add);

    std::wstring   Title() const override    { return L"إضافة صوت برنامج"; }
    std::wstring   Subtitle() const override { return L"يُسجَّل صوت البرنامج وحده، دون إشعارات ويندوز أو الأصوات الأخرى"; }
    const wchar_t* IconGlyph() const override { return Icon::Speaker; }
    float          Width() const override    { return 540; }
    float          BodyHeight() const override;
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;

private:
    void          Refresh();                         // منجيب اللائحة من جديد
    void          UpdatePlaying();                   // بس مين عم يطلّع صوت (أخف)
    float         DrawRow(float y, float L, float R, int index);

    std::function<bool(const std::wstring&)> m_isAdded;
    std::function<void(const AudioApp&)>     m_add;
    std::vector<AudioApp> m_apps;
    int  m_playingCount = 0;
    int  m_frame = 0;

    IconCache m_icons;     // أيقونات البرامج
};
