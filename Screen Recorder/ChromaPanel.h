// =============================================================
//  ChromaPanel.h - نافذة "إزالة الخلفية الخضراء" للكاميرا
//
//  معاينة حيّة للكاميرا فوق خلفية مربّعات (يعني الشفاف بيبيّن)،
//  وبتكبس على الخلفية بالصورة لتختار لونها (قطّارة)،
//  أو "تلقائي" ، أو أخضر / أزرق. وتحت منزلقات للدقّة والنعومة والانعكاس.
// =============================================================
#pragma once
#include "Panel.h"
#include "Capture.h"

struct ChromaHooks
{
    std::function<bool(SceneLayer&)>                          get;      // نسخة من طبقة الكاميرا (false = انحذفت)
    std::function<void(const std::function<void(SceneLayer&)>&)> modify;   // تعديل الطبقة الحقيقية
    std::function<void()>                                     save;
};

class ChromaPanel : public Panel
{
public:
    explicit ChromaPanel(ChromaHooks hooks) : m_h(std::move(hooks)) {}

    std::wstring   Title() const override    { return L"إزالة الخلفية الخضراء"; }
    std::wstring   Subtitle() const override { return L"اضغط على الخلفية في الصورة لاختيار لونها"; }
    const wchar_t* IconGlyph() const override { return L""; }
    float          Width() const override    { return 560; }
    float          BodyHeight() const override { return 560; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    bool           OnDrag(int id, D2D1_POINT_2F p) override;
    void           OnCommit() override { if (m_h.save) m_h.save(); }

private:
    ChromaHooks          m_h;
    D2D1_RECT_F          m_previewDst{};        // وين انرسمت الصورة (للقطّارة)
    int                  m_frameW = 0, m_frameH = 0;
    unsigned long long   m_version = 0;
    ComPtr<ID2D1Bitmap>  m_bitmap;
    ID2D1HwndRenderTarget* m_owner = nullptr;
};
