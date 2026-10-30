// =============================================================
//  Overlay.h - مؤشر التسجيل الصغير + الإشعارات على الشاشة
//
//  1) الشارة: نقطة حمرا بتنبض + الوقت + "مباشر" + الإعادة + المايك
//  2) الإشعار: بطاقة أكبر شوي بتطلع بنفس الزاوية لكم ثانية:
//     "بدأ التسجيل"، "تم حفظ التسجيل"، "حُفظت الإعادة"، "الميكروفون مكتوم"...
//
//  - الماوس بيمرق من خلالهم (ما بيزعجوك ولا بياخدوا الكبسات)
//  - ما بيطلعوا بالتسجيل ولا بالبث (ويندوز بيخبيهم عن التصوير)
//  - فوق كلشي دايمًا: منرجّعهم لفوق كل ربع ثانية، وكل إطار وقت الإشعار،
//    وفورًا كل ما تبدّلت النافذة اللي قدامك (الألعاب والبرامج التانية بتحاول تطلع فوق)
//  - بيطلعوا على الشاشة اللي إنت عم تشتغل عليها (شاشة النافذة اللي قدامك)،
//    وإذا سطح المكتب قدامك: الشاشة اللي عم تتسجّل، وإلا الرئيسية
//  - الإشعارات بتنصف ورا بعض (ما حدا بيمحي التاني)
//  ملاحظة: الألعاب بوضع "ملء الشاشة الحصري" (Exclusive Fullscreen) ما بتسمح لأي نافذة
//  تطلع فوقها (حتى ويندوز نفسه)، بس بوضع "Borderless / Windowed Fullscreen" بيبيّنوا عادي.
//  فمنكشف هالوضع، وإذا إشعار "ضاع" وراه، لما تطلع من اللعبة منقلّك ليش (مرة وحدة).
// =============================================================
#pragma once
#include "Ui.h"
#include <deque>

struct OverlayState
{
    bool      recording = false;
    bool      paused    = false;
    bool      streaming = false;
    double    uploadKbps = 0;      // سرعة الرفع الحالية للبث (مجموع كل المنصات)
    LONGLONG  elapsedMs = 0;
    bool      micOn     = false;   // المايك مفعّل وشغّال
    bool      micMuted  = false;
    float     micLevel  = 0;       // 0..1
    bool      replay    = false;   // الإعادة شغّالة (عم تسجّل بالخلفية)
    bool      replaySaved = false; // حفظت مقطع هلق (منبيّن "حُفظ" ثانيتين)
    HMONITOR  monitor   = nullptr; // على أي شاشة (فاضي = الرئيسية)
};

class RecordingOverlay
{
public:
    ~RecordingOverlay() { Destroy(); }

    // corner: 0 = الشارة مخفية ، 1 أعلى اليمين ، 2 أعلى اليسار ، 3 أسفل اليمين ، 4 أسفل اليسار
    // owner: نافذة البرنامج (إذا هي اللي قدامك منخبي كلشي، لأنك شايف كلشي أصلاً)
    void Update(const OverlayState& s, int corner, HWND owner);
    void Destroy();

    // إشعار: glyph = أيقونة من Segoe MDL2 ، color = لون الشريط والأيقونة
    // force: بيطلع حتى لو البرنامج قدامك (للتجربة من الإعدادات)
    void Toast(wchar_t glyph, D2D1_COLOR_F color, const std::wstring& title, const std::wstring& sub = L"", bool force = false);

private:
    bool Create();
    void Render(const OverlayState& s, int corner, bool showPill, float toastAlpha, float toastSlide);
    void DrawPill(const OverlayState& s, float x, float y, float w);
    float PillWidth(const OverlayState& s) const;
    void DrawToast(float x, float y, float alpha);

    HWND  m_hwnd  = nullptr;
    bool  m_shown = false;
    int   m_frame = 0;
    UINT  m_dpi   = 96;
    HWND  m_lastFg = nullptr;     // آخر نافذة كانت قدامك
    bool  m_fgChanged = false;
    ComPtr<ID2D1Factory>         m_factory;
    ComPtr<ID2D1DCRenderTarget>  m_rt;
    ComPtr<ID2D1SolidColorBrush> m_brush;
    ComPtr<IDWriteTextFormat>    m_fTime, m_fBadge, m_fIcon, m_fToastTitle, m_fToastSub, m_fToastIcon;

    // الإشعار الحالي ، والي ناطرين دورهم
    struct ToastInfo
    {
        wchar_t      glyph = 0;
        D2D1_COLOR_F color{};
        std::wstring title, sub;
        ULONGLONG    start = 0;
        bool         force = false;
    } m_toast;
    std::deque<ToastInfo> m_queue;
    void NextToast();

    // لعبة بوضع "ملء الشاشة الحصري" قدامك؟ (ما في شي بيطلع فوقها)
    void CheckExclusive();
    int   m_exCheck   = 0;
    bool  m_exclusive = false;   // هلق
    bool  m_missed    = false;   // إشعار طلع وهي قدامك (ما شفته)
    bool  m_tipShown  = false;   // قلنالك ليش (مرة وحدة لكل تشغيل)
};
