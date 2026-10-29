// =============================================================
//  Overlay.h - مؤشر التسجيل الصغير على الشاشة
//
//  شارة صغيرة شفافة بزاوية الشاشة (فوق الألعاب وسطح المكتب):
//   نقطة حمرا بتنبض + الوقت + حالة المايك (وبيضوي لما تحكي)
//   + أيقونة الإعادة إذا شغّالة (وبتضوي أخضر لما تحفظ مقطع بـ F7)
//
//  - الماوس بيمرق من خلالها (ما بتزعجك ولا بتاخد الكبسات)
//  - ما بتطلع بالتسجيل ولا بالبث (ويندوز بيخبيها عن التصوير)
//  - بتطلع على الشاشة اللي عم تتسجّل (اللي بالمشهد)، وإذا ما في: الرئيسية
//  ملاحظة: الألعاب بوضع "ملء الشاشة الحصري" ما بتسمح لشي يطلع فوقها،
//  بس بوضع "Borderless / Windowed Fullscreen" بتبيّن عادي.
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

    // corner: 0 = مخفي ، 1 أعلى اليمين ، 2 أعلى اليسار ، 3 أسفل اليمين ، 4 أسفل اليسار
    // owner: نافذة البرنامج (إذا هي اللي قدامك منخبي المؤشر، لأنك شايف كلشي أصلاً)
    void Update(const OverlayState& s, int corner, HWND owner);
    void Destroy();

private:
    bool Create();
    void Render(const OverlayState& s, int corner);

    HWND  m_hwnd  = nullptr;
    bool  m_shown = false;
    int   m_frame = 0;
    UINT  m_dpi   = 96;
    HWND  m_lastFg = nullptr;     // آخر نافذة كانت قدامك
    bool  m_fgChanged = false;
    ComPtr<ID2D1Factory>        m_factory;
    ComPtr<ID2D1DCRenderTarget> m_rt;
    ComPtr<IDWriteTextFormat>   m_fTime, m_fBadge, m_fIcon;
};
