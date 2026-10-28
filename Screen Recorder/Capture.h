// =============================================================
//  Capture.h - كل شي بخص "شو منصوّر":
//   - قائمة الشاشات والنوافذ
//   - FrameGrabber: بياخد صورة من شاشة أو نافذة
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX      // بيمنع ويندوز يخرّب std::min و std::max
#endif

#include <windows.h>
#include <string>
#include <vector>

// مصدر التسجيل: شاشة كاملة أو نافذة
struct CaptureTarget
{
    bool         isMonitor = true;   // true = شاشة كاملة ، false = نافذة
    bool         isPrimary = false;  // الشاشة الرئيسية؟
    RECT         rect{};             // مكان وحجم الشاشة
    HWND         hwnd = nullptr;     // رقم النافذة عند ويندوز
    std::wstring name;               // الاسم اللي بيظهر بالقائمة
    std::wstring className;          // نوع النافذة (لحتى نلاقيها بعد ما تنفتح من جديد)
    std::wstring exePath;            // البرنامج اللي فاتحها
};

// نوع الطبقة
enum class LayerKind
{
    Capture,    // شاشة أو نافذة
    Camera,     // كاميرا (ويب كام)
    Image,      // صورة من ملف
    Text        // نص
};

// شكل الطبقة (للكاميرا والصور)
enum LayerShape { SHAPE_RECT = 0, SHAPE_ROUNDED = 1, SHAPE_CIRCLE = 2 };

// طبقة بالمشهد (متل OBS): مصدر + مكانه وحجمه جوّا لوحة الفيديو
struct SceneLayer
{
    int           id = 0;
    LayerKind     kind = LayerKind::Capture;
    CaptureTarget target;                        // للشاشة والنافذة (و target.name = اسم الطبقة لكل الأنواع)
    double        x = 0, y = 0, w = 0, h = 0;   // ببكسل الفيديو
    bool          visible = true;
    bool          lost    = false;               // النافذة انسكّرت / الكاميرا مفصولة / الملف مش موجود

    // للكاميرا والصورة والنص
    std::wstring  source;                        // الكاميرا: رابط الجهاز ، الصورة: المسار ، النص: النص نفسه
    int           shape = SHAPE_RECT;
    bool          mirror = false;                // الكاميرا: عكس متل المراية
    unsigned      color = 0xFFFFFFFF;            // النص: اللون (ARGB)
    bool          background = false;            // النص: خلفية غامقة وراه
};

// بيرجّع كل الشاشات، وبعدها كل النوافذ المفتوحة (ما عدا exclude)
std::vector<CaptureTarget> ListCaptureTargets(HWND exclude);

// منلاقي نافذة مفتوحة بتطابق نافذة محفوظة (نفس النوع والعنوان، أو نفس النوع والبرنامج)
HWND FindMatchingWindow(const CaptureTarget& saved, HWND exclude);

// هل المصدرين نفس الشي؟
bool SameTarget(const CaptureTarget& a, const CaptureTarget& b);

// حجم المصدر الحقيقي بالبكسل
bool GetCaptureSize(const CaptureTarget& target, int& width, int& height);

// تردد الشاشة اللي عليها المصدر (مثلاً 60 أو 144 هرتز)
UINT32 GetRefreshRate(const CaptureTarget& target);

// -------------------------------------------------------------
// FrameGrabber: بيرسم صورة المصدر على "لوحة" (HDC) بالحجم المطلوب
// -------------------------------------------------------------
class FrameGrabber
{
public:
    FrameGrabber();
    ~FrameGrabber();
    FrameGrabber(const FrameGrabber&) = delete;
    FrameGrabber& operator=(const FrameGrabber&) = delete;

    // بيرسم المصدر بالمستطيل (x, y, w, h) على dst
    // بيرجّع false إذا النافذة انسكّرت
    bool Grab(const CaptureTarget& target, HDC dst, int x, int y, int w, int h, bool drawCursor);

private:
    HDC     m_screen = nullptr;
    HDC     m_winDC  = nullptr;
    HBITMAP m_winBmp = nullptr;
    HGDIOBJ m_oldBmp = nullptr;
    int     m_winW = 0, m_winH = 0;
    RECT    m_lastVis{};                 // آخر مكان للنافذة (لما تتصغّر منستعمله)
    int     m_lastOx = 0, m_lastOy = 0;
};
