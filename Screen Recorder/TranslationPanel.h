// =============================================================
//  TranslationPanel.h - نافذة "ترجمة اللعبة للعربية"
//
//  كل إعدادات الترجمة بمكان واحد ، مرتّبة بأقسام:
//   - الملف الشخصي (لكل لعبة ملف)
//   - المصدر ومكان الترجمة: بتسحب مستطيل على الصورة الحيّة ،
//     وتحتها بتشوف شو عم يقرا البرنامج وشو الترجمة (مباشرة)
//   - خدمة الترجمة: 7 خدمات ، مع المفتاح والنموذج وزر "تجربة"
//   - الأسلوب: اللهجة ، اسم اللعبة ، تلطيف الألفاظ
//   - الأسماء والمصطلحات (Jason = جيسون)
//   - المزامنة: تأخير الصورة والصوت
//   - الشكل
//   - الترجمة على شاشتك (فوق اللعبة إلك إنت)
//   - قراءة النص
//   - ترجمة الشاشة كلها (شرح واختصارها)
// =============================================================
#pragma once
#include "Panel.h"
#include "Translator.h"
#include "Media.h"
#include "Capture.h"

struct TranslationPanelHooks
{
    std::function<std::vector<CameraInfo>()>       devices;       // أجهزة الالتقاط والكاميرات بالمشهد
    std::function<std::vector<CaptureTarget>()>    monitors;      // الشاشات
    std::function<bool(const std::wstring& title, std::wstring& text, bool password, bool ltr, int limit)> input;
    std::function<int(const std::vector<std::wstring>& items, int checked)> menu;   // قائمة عند الماوس
    std::function<bool()>                          busy;          // تسجيل أو بث
    std::function<void(const std::wstring& id)>    addToScene;    // إضافة طبقة ترجمة بهالملف
    std::function<void(const std::wstring& id)>    fitLayer;      // ترجع الطبقة فوق مكان الترجمة
    std::function<void(const std::wstring& id)>    useProfile;    // الطبقة صارت تستعمل هالملف
    std::function<std::wstring()>                  screenHotkey;  // اختصار "ترجمة الشاشة كلها" (فاضي = بلا)
};

class TranslationPanel : public Panel
{
public:
    // addMode: النافذة فتحت من "إضافة مصدر" (فيها زر "إضافة إلى المشهد")
    TranslationPanel(TranslationPanelHooks hooks, const std::wstring& profileId, bool addMode);
    ~TranslationPanel() override;

    std::wstring   Title() const override    { return L"ترجمة اللعبة إلى العربية"; }
    std::wstring   Subtitle() const override { return L"يقرأ الترجمة الإنكليزية من الشاشة ويعرضها بالعربية للمشاهدين"; }
    const wchar_t* IconGlyph() const override { return L"\xE8C1"; }
    float          Width() const override    { return 680; }
    float          BodyHeight() const override { return m_height; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    bool           OnDrag(int id, D2D1_POINT_2F p) override;
    void           OnCommit() override;

private:
    struct TestState;

    bool  Load();                                     // m_p من المخزن
    void  Apply(bool persist);                        // m_p للمخزن
    void  NewProfile();
    float DrawProfiles(float y, float L, float R);
    float DrawSource(float y, float L, float R);
    float DrawEngine(float y, float L, float R);
    float DrawStyle(float y, float L, float R);
    float DrawGlossary(float y, float L, float R);
    float DrawSync(float y, float L, float R);
    float DrawLook(float y, float L, float R);
    float DrawPlayer(float y, float L, float R);
    float DrawReading(float y, float L, float R);
    float DrawScreenMode(float y, float L, float R);
    float ToggleRow(float y, float L, float R, const std::wstring& label, const std::wstring& sub, bool on, int id);
    void  RunTest();

    TranslationPanelHooks          m_h;
    std::wstring                   m_id;
    bool                           m_addMode = false;
    TranslateProfile               m_p;
    std::vector<TranslateProfile>  m_all;
    std::vector<CameraInfo>        m_devices;
    std::vector<CaptureTarget>     m_monitors;      // مرة وحدة (البحث عن الشاشات كل رسمة تقيل)
    float                          m_height = 900;
    int                            m_frame = 0;

    // المعاينة وتحديد المكان
    ComPtr<ID2D1Bitmap>            m_bitmap;
    ID2D1HwndRenderTarget*         m_owner = nullptr;
    unsigned long long             m_version = 0;
    int                            m_bmpW = 0, m_bmpH = 0;
    D2D1_RECT_F                    m_imgDst{};
    bool                           m_dragging = false;
    bool                           m_dragMoved = false;     // السحب غيّر المكان (لما يخلص: منحرّك طبقة البث)
    D2D1_POINT_2F                  m_anchor{};

    std::shared_ptr<TestState>     m_test;
};
