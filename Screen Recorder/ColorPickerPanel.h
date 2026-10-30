// =============================================================
//  ColorPickerPanel.h - نافذة "اختيار لون"
//
//  دائرة ألوان: الزاوية = اللون ، البعد عن النص = قوّة اللون.
//  وتحتها منزلق للسطوع. اللون بيتطبّق فوراً وانت عم تسحب
//  (لحتى تشوف شكل البرنامج قبل ما تقرّر).
// =============================================================
#pragma once
#include "Panel.h"

class ColorPickerPanel : public Panel
{
public:
    // initial: اللون الحالي (0xRRGGBB) ، changed: كل ما تغيّر اللون ، done: لما تكبس "تم" أو تسكّر
    ColorPickerPanel(UINT32 initial, std::function<void(UINT32)> changed, std::function<void(UINT32, bool)> done);

    std::wstring   Title() const override    { return L"لون مخصّص"; }
    std::wstring   Subtitle() const override { return L"اسحب داخل الدائرة لاختيار اللون، والمنزلق للسطوع"; }
    float          Width() const override    { return 460; }
    float          BodyHeight() const override { return 420; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    bool           OnDrag(int id, D2D1_POINT_2F p) override;
    ~ColorPickerPanel() override;

private:
    UINT32 Current() const;
    void   SetFromRgb(UINT32 rgb);
    void   EnsureWheel();

    float  m_h = 0, m_s = 0, m_v = 1;          // اللون (0..1)
    UINT32 m_initial;
    std::function<void(UINT32)>       m_changed;
    std::function<void(UINT32, bool)> m_done;  // bool = تم (وإلا إلغاء)
    bool   m_finished = false;

    D2D1_POINT_2F m_center{};
    float         m_radius = 0;
    ComPtr<ID2D1Bitmap>       m_wheel;
    ID2D1HwndRenderTarget*    m_wheelOwner = nullptr;
};
