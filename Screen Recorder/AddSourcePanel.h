// =============================================================
//  AddSourcePanel.h - نافذة "إضافة مصدر"
//
//  كل المصادر بمكان واحد ومرتّبة:
//   - إضافة سريعة: صورة، نص، صوت برنامج
//   - الشاشات (مع لقطة مصغّرة لكل شاشة)
//   - الكاميرات
//   - النوافذ المفتوحة (بأيقونة البرنامج وعنوان النافذة)
//  وفوقهن خانة بحث.
// =============================================================
#pragma once
#include "Panel.h"
#include "IconCache.h"
#include "Capture.h"
#include "Media.h"

// شو بيصير لما تختار كل نوع (main.cpp بيعرّفهم)
struct AddSourceActions
{
    std::function<void(const CaptureTarget&)> capture;    // شاشة أو نافذة
    std::function<void(const CameraInfo&)>    camera;
    std::function<void()>                     image;
    std::function<void()>                     text;
    std::function<void()>                     appAudio;
    std::function<void()>                     region;     // جزء من الشاشة
    std::function<void()>                     web;        // صفحة ويب (تنبيهات، شات...)
};

class AddSourcePanel : public Panel
{
public:
    AddSourcePanel(HWND exclude, AddSourceActions actions);

    std::wstring   Title() const override    { return L"إضافة مصدر"; }
    std::wstring   Subtitle() const override { return L"اختر ما تريد إضافته إلى المشهد"; }
    const wchar_t* IconGlyph() const override { return Icon::Add; }
    float          Width() const override    { return 620; }
    float          BodyHeight() const override;
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    bool           OnChar(wchar_t c) override;

private:
    struct Thumb { int w = 0, h = 0; std::vector<BYTE> pixels; };

    bool  Matches(const std::wstring& a, const std::wstring& b = L"") const;   // بيطابق البحث؟
    float Layout(float L, float top, float R, bool draw);                      // الرسم (أو بس حساب الطول)
    float DrawRow(float y, float L, float R, int id, ID2D1Bitmap* icon, const wchar_t* glyph,
                  const std::wstring& title, const std::wstring& sub, bool draw);

    AddSourceActions           m_actions;
    std::vector<CaptureTarget> m_monitors, m_windows;
    std::vector<Thumb>         m_thumbs;           // لقطة لكل شاشة
    std::vector<CameraInfo>    m_cameras;
    std::vector<std::wstring>  m_appNames;         // اسم برنامج كل نافذة
    std::wstring               m_search;
    IconCache                  m_icons;
    int                        m_frame = 0;
    float                      m_height = 400;     // آخر طول محسوب
    float                      m_maxHeight = 0;    // أطول شي وصلناله (لحتى ما تصغر النافذة وانت عم تبحث)
};
