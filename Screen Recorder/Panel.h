// =============================================================
//  Panel.h - النوافذ الداخلية
//
//  "نافذة داخلية" = كرت بينفتح بنص البرنامج فوق كلشي،
//  والخلفية بتغمق. مش نافذة ويندوز منفصلة، فبتضل بنفس الثيم.
//
//  لتعمل نافذة جديدة (متل فلاتر الميكروفون):
//    1) اعمل class بيورث من Panel
//    2) عرّف Title() و BodyHeight() و DrawBody() و OnClick()
//    3) افتحها بـ g_panels.Open(std::make_unique<نافذتك>(...))
//
//  الـ Panel بيعطيك عناصر جاهزة: قسم بعنوان ومفتاح، منزلق، زر.
//  والـ PanelHost بيهتم بالباقي: الرسم، الماوس، التمرير، الإغلاق.
// =============================================================
#pragma once
#include "Ui.h"
#include <functional>
#include <memory>

// أرقام عناصر النوافذ الداخلية (بعيدة عن أرقام الواجهة الرئيسية)
enum : int
{
    ID_PANEL_BACKDROP = 50000,   // الخلفية الغامقة (كبسة عليها = إغلاق)
    ID_PANEL_CARD,               // جسم النافذة (ما بيعمل شي، بس بيمنع الكبس يوصل للخلفية)
    ID_PANEL_CLOSE,              // زر الإغلاق
    ID_PANEL_USER = 50100        // من هون وطالع: عناصر النافذة نفسها
};

class Panel
{
public:
    virtual ~Panel() = default;

    // ---- لازم كل نافذة تعرّفهم ----
    virtual std::wstring   Title() const = 0;
    virtual std::wstring   Subtitle() const { return L""; }
    virtual const wchar_t* IconGlyph() const { return nullptr; }
    virtual float          Width() const { return 520; }
    virtual float          BodyHeight() const = 0;                 // طول المحتوى كله
    virtual void           DrawBody(float left, float top, float right) = 0;
    virtual void           OnClick(int /*id*/) {}
    virtual void           OnCommit() {}                          // بعد أي تغيير (منحفظ الإعدادات)
    virtual bool           OnChar(wchar_t /*c*/) { return false; }  // كتابة (مثلاً خانة بحث)
    virtual bool           OnKeyDown(WPARAM /*vk*/) { return false; }  // كبسة زر (true = عالجتها)
    virtual bool           CapturingKeys() const { return false; }     // بدها كل الأزرار (حتى Alt و F10)

protected:
    // ---- عناصر جاهزة لكل النوافذ ----

    // كرت قسم: عنوان وشرح على اليمين، ومفتاح تشغيل وزر "افتراضي" على اليسار
    // بيرجّع y تحت رأس القسم
    float Section(const D2D1_RECT_F& card, const std::wstring& title, const std::wstring& sub,
                  bool on, int toggleId, int resetId);

    // سطر منزلق: الاسم على اليمين، القيمة على اليسار، والمنزلق بالنص
    // الأصغر على اليمين والأكبر على اليسار (متل شريط العلو)
    // بيرجّع y تحت السطر
    float SliderRow(float y, float left, float right, const std::wstring& label, const std::wstring& valueText,
                    float value, float minV, float maxV, float step, int id, std::function<void(float)> set,
                    bool dim = false);

    // منزلق لحالو بأي مكان (مثلاً فوق مقياس الصوت)
    void  AddSlider(const D2D1_RECT_F& hitRect, const D2D1_RECT_F& track, float value, float minV, float maxV,
                    float step, int id, std::function<void(float)> set);

    // النافذة بدها تتسكّر (مثلاً بعد ما اخترت شي). بتتسكّر بعد ما تخلص الكبسة
    void RequestClose() { m_closeRequested = true; }
    // سكّر النافذة، وبعدها نفّذ شي (مثلاً افتح نافذة تانية أو صندوق اختيار ملف)
    void CloseThen(std::function<void()> fn) { m_afterClose = std::move(fn); m_closeRequested = true; }

    // قيمة المنزلق من مكان الماوس
    static float ValueAtX(const D2D1_RECT_F& track, float x, float minV, float maxV, float step);

private:
    friend class PanelHost;
    struct SliderInfo
    {
        int                        id;
        D2D1_RECT_F                track;
        float                      value, minV, maxV, step;
        std::function<void(float)> set;
    };
    std::vector<SliderInfo> m_sliders;            // بيتعبّى من جديد كل رسمة
    const SliderInfo* FindSlider(int id) const;
    int  m_activeSlider = 0;                      // المنزلق اللي عم ينسحب
    bool m_closeRequested = false;
    std::function<void()> m_afterClose;
};

// -------------------------------------------------------------
//  المسؤول عن النافذة المفتوحة (وحدة بالمرة)
// -------------------------------------------------------------
class PanelHost
{
public:
    void Open(std::unique_ptr<Panel> panel);
    void Close();
    bool IsOpen() const { return m_panel != nullptr; }
    bool IsDragging() const { return m_panel && m_panel->m_activeSlider != 0; }

    // بيترسم بعد كل الواجهة (فوقها)
    void Draw(float W, float H);

    // الماوس والكيبورد (بس وقت النافذة مفتوحة)
    void OnMouseDown(D2D1_POINT_2F p);
    void OnMouseMove(D2D1_POINT_2F p);
    void OnMouseUp(D2D1_POINT_2F p);
    void OnWheel(D2D1_POINT_2F p, float steps);
    void OnKey(WPARAM key);
    void OnChar(wchar_t c);
    bool WantsKeys() const { return m_panel && m_panel->CapturingKeys(); }
    // شكل الماوس: true = إيد (فوق شي بينكبس)
    bool WantsHandCursor(D2D1_POINT_2F p) const;

private:
    void ApplySlider(D2D1_POINT_2F p);

    std::unique_ptr<Panel> m_panel;
    int         m_pressed = 0;                  // شو كان تحت الماوس لما انكبس
    float       m_scroll = 0, m_scrollMax = 0;
    D2D1_RECT_F m_body{};
};

extern PanelHost g_panels;
