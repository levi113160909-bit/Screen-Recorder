// =============================================================
//  Overlay.h - مؤشر التسجيل الصغير + الإشعارات على الشاشة
//
//  1) الشارة: نقطة حمرا بتنبض + الوقت + "مباشر" + الإعادة + المايك
//  2) الإشعار: بطاقة أكبر شوي بتطلع بنفس الزاوية لكم ثانية:
//     "بدأ التسجيل"، "تم حفظ التسجيل"، "حُفظت الإعادة"، "الميكروفون مكتوم"...
//
//  - الماوس بيمرق من خلالهم (ما بيزعجوك ولا بياخدوا الكبسات)
//  - ما بيطلعوا بالتسجيل ولا بالبث (ويندوز بيخبيهم عن التصوير)
//  - بيطلعوا على الشاشة اللي عم تتسجّل (اللي بالمشهد)، وإذا ما في: الرئيسية
//  ملاحظة: الألعاب بوضع "ملء الشاشة الحصري" ما بتسمح لشي يطلع فوقها،
//  بس بوضع "Borderless / Windowed Fullscreen" بيبيّنوا عادي.
// =============================================================
#pragma once
#include "Ui.h"

struct OverlayState
{
    bool      recording = false;
    bool      paused    = false;
    bool      streaming = false;
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

    // الإشعار الحالي
    struct ToastInfo
    {
        wchar_t      glyph = 0;
        D2D1_COLOR_F color{};
        std::wstring title, sub;
        ULONGLONG    start = 0;
        bool         force = false;
    } m_toast;
};
