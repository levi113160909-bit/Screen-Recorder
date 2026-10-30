// =============================================================
//  Overlay.cpp - مؤشر التسجيل + الإشعارات (شوف Overlay.h)
// =============================================================
#include "Overlay.h"
#include <shellscalingapi.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "shell32.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace
{
    const wchar_t OVERLAY_CLASS[] = L"ScreenRecorderOverlay";
    constexpr float H        = 32;      // ارتفاع الشارة (بالنقاط)
    constexpr float MARGIN   = 18;      // بُعدها عن حافة الشاشة
    constexpr float TOAST_W  = 320, TOAST_H = 68;
    constexpr float GAP      = 10;      // بين الشارة والإشعار
    constexpr ULONGLONG TOAST_MS = 3200;   // قديش بيضل الإشعار
    constexpr ULONGLONG TOAST_MIN_MS = 1600;   // إذا في إشعار ناطر: الحالي بيضل هالقد بس
    constexpr ULONGLONG FADE_MS  = 350;
    constexpr size_t    QUEUE_MAX = 4;         // أكتر من هيك: منشيل الأقدم (ما بنكوّم إشعارات قديمة)

    // الشاشة اللي إنت عم تشتغل عليها: شاشة النافذة اللي قدامك
    // (إلا إذا سطح المكتب أو شريط المهام قدامك: وقتها الشاشة اللي عم تتسجّل)
    HMONITOR UserMonitor(HMONITOR fallback)
    {
        if (HWND fg = GetForegroundWindow(); fg && IsWindowVisible(fg) && !IsIconic(fg))
        {
            wchar_t cls[64] = {};
            GetClassNameW(fg, cls, 64);
            if (wcscmp(cls, L"Progman") != 0 && wcscmp(cls, L"WorkerW") != 0 &&
                wcscmp(cls, L"Shell_TrayWnd") != 0 && wcscmp(cls, L"Shell_SecondaryTrayWnd") != 0)
                return MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
        }
        return fallback ? fallback : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    }

    LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCHITTEST)
            return HTTRANSPARENT;     // الماوس بيمرق من خلالها
        if (msg == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    std::wstring Clock(LONGLONG ms)
    {
        const LONGLONG s = ms / 1000;
        wchar_t buf[32];
        if (s >= 3600) swprintf_s(buf, L"%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
        else           swprintf_s(buf, L"%02lld:%02lld", s / 60, s % 60);
        return buf;
    }
}

bool RecordingOverlay::Create()
{
    if (m_hwnd)
        return true;
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc   = OverlayProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = OVERLAY_CLASS;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             OVERLAY_CLASS, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_hwnd)
        return false;
    // ما بتطلع بالتسجيل ولا بالبث ولا بلقطات الشاشة (ويندوز 10 نسخة 2004 وطالع)
    SetWindowDisplayAffinity(m_hwnd, WDA_EXCLUDEFROMCAPTURE);
    if (!m_factory)
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_factory.GetAddressOf());
    return true;
}

void RecordingOverlay::Destroy()
{
    m_brush.Reset();
    m_rt.Reset();
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_shown = false;
}

void RecordingOverlay::Toast(wchar_t glyph, D2D1_COLOR_F color, const std::wstring& title, const std::wstring& sub, bool force)
{
    ToastInfo t;
    t.force = force;
    t.glyph = glyph;
    t.color = color;
    t.title = title;
    t.sub   = sub;
    // نفس نوع الإشعار مبيّن هلق (متل "علامة 3" بعد "علامة 2" ، أو "الميكروفون يعمل" بعد "مكتوم"):
    // منحدّثه بمكانه بدل ما نصفّه. النوع = أول كلمة من العنوان
    auto firstWord = [](const std::wstring& s) { return s.substr(0, s.find(L' ')); };
    if (m_toast.start && m_queue.empty() && firstWord(m_toast.title) == firstWord(title))
    {
        t.start = GetTickCount64() - std::min<ULONGLONG>(GetTickCount64() - m_toast.start, FADE_MS);
        m_toast = t;
        return;
    }
    m_queue.push_back(t);
    while (m_queue.size() > QUEUE_MAX)
        m_queue.pop_front();
    if (!m_toast.start)
        NextToast();
}

void RecordingOverlay::NextToast()
{
    if (m_queue.empty())
    {
        m_toast.start = 0;
        return;
    }
    m_toast = m_queue.front();
    m_queue.pop_front();
    m_toast.start = GetTickCount64();
}

void RecordingOverlay::CheckExclusive()
{
    if (m_exCheck++ % 15 == 0)                // كل نص ثانية بتكفّي
    {
        QUERY_USER_NOTIFICATION_STATE st{};
        const bool ex = SUCCEEDED(SHQueryUserNotificationState(&st)) && st == QUNS_RUNNING_D3D_FULL_SCREEN;
        if (m_exclusive && !ex && m_missed && !m_tipShown)
        {
            // طلعت من اللعبة: منقلّك ليش ما شفت الإشعارات (مرة وحدة)
            m_tipShown = true;
            m_missed = false;
            Toast(0xE7BA, Theme::Yellow, L"لم تظهر الإشعارات فوق اللعبة",
                  L"اللعبة بوضع «ملء الشاشة الحصري»: اختر «بلا حواف» من إعداداتها");
        }
        m_exclusive = ex;
    }
    if (m_exclusive && m_toast.start)
        m_missed = true;
}

void RecordingOverlay::Update(const OverlayState& s, int corner, HWND owner)
{
    CheckExclusive();

    // نافذة البرنامج قدامك فعلاً؟ (المصغّرة بتضل "قدام" عند ويندوز لحد ما تكبس على شي تاني،
    // فمنتأكد إنها مش مصغّرة وإنها مبيّنة)
    const bool ownerInFront = owner && GetForegroundWindow() == owner && !IsIconic(owner) && IsWindowVisible(owner);

    // تبدّلت النافذة اللي قدامك؟ منرجّع المؤشر فوق كلشي فوراً
    const HWND fgNow = GetForegroundWindow();
    if (fgNow != m_lastFg)
    {
        m_lastFg = fgNow;
        m_fgChanged = true;
    }

    // الإشعار: بيطلع شوي شوي، بيضل، وبيختفي شوي شوي
    float toastAlpha = 0, toastSlide = 0;
    if (m_toast.start)
    {
        // في إشعار ناطر؟ الحالي بيقصر (بس بيضل وقت كافي ينقرا)
        const ULONGLONG life = m_queue.empty() ? TOAST_MS : TOAST_MIN_MS;
        const ULONGLONG t = GetTickCount64() - m_toast.start;
        if (t >= life)
            NextToast();
        if (m_toast.start && GetTickCount64() - m_toast.start < life)
        {
            const ULONGLONG tt = GetTickCount64() - m_toast.start;
            const float in  = std::min(1.0f, tt / (float)FADE_MS);
            const float out = std::min(1.0f, (life - tt) / (float)FADE_MS);
            toastAlpha = std::min(in, out);
            toastSlide = (1 - in) * (1 - in);           // بيدخل من الطرف بنعومة
        }
    }
    if (ownerInFront && !m_toast.force)
        toastAlpha = 0;          // البرنامج قدامك: الرسالة بتبيّن جوّاته

    const bool showPill = corner > 0 && (s.recording || s.streaming || s.replay) && !ownerInFront;
    if (!showPill && toastAlpha <= 0)
    {
        if (m_shown)
        {
            ShowWindow(m_hwnd, SW_HIDE);
            m_shown = false;
        }
        return;
    }
    if (!Create())
        return;
    ++m_frame;
    // الشارة لحالها: 15 مرة بالثانية بتكفّي. وقت الإشعار: كل إطار (حركة ناعمة)
    if (m_frame % 2 == 0 || !m_shown || m_fgChanged || toastAlpha > 0)
        Render(s, corner > 0 ? corner : 1, showPill, toastAlpha, toastSlide);
}

float RecordingOverlay::PillWidth(const OverlayState& s) const
{
    const bool showDot = s.recording || s.streaming;
    float w = showDot ? 26.0f : 8.0f;
    if (s.recording) w += 52;
    if (s.streaming) w += 52 + 96;                      // "مباشر" + سرعة الرفع
    if (s.replay)    w += s.replaySaved ? 66 : 30;
    return w + 38;                                       // المايك + هامش
}

// -------------------------------------------------------------
//  الشارة
// -------------------------------------------------------------
void RecordingOverlay::DrawPill(const OverlayState& s, float x0, float y0, float w)
{
    ID2D1SolidColorBrush* b = m_brush.Get();
    auto fill = [&](const D2D1_RECT_F& rr, D2D1_COLOR_F c, float rad)
    {
        b->SetColor(c);
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(rr, rad, rad), b);
    };
    auto text = [&](const std::wstring& t, IDWriteTextFormat* f, const D2D1_RECT_F& rr, D2D1_COLOR_F c)
    {
        b->SetColor(c);
        m_rt->DrawTextW(t.c_str(), (UINT32)t.size(), f, rr, b);
    };
    auto R = [&](float l, float t, float r, float bt) { return D2D1::RectF(x0 + l, y0 + t, x0 + r, y0 + bt); };

    const D2D1_RECT_F pill = R(0.5f, 0.5f, w - 0.5f, H - 0.5f);
    fill(pill, D2D1::ColorF(0x0E1014, 0.72f), H / 2);
    b->SetColor(D2D1::ColorF(0xFFFFFF, 0.10f));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(pill, H / 2, H / 2), b, 1);

    const bool showDot = s.recording || s.streaming;
    float cx = showDot ? 12.0f : 8.0f;
    const float cy = H / 2;
    if (showDot)
    {
        if (s.paused && s.recording)          // إيقاف مؤقت: علامة صفرا
        {
            fill(R(cx, cy - 5, cx + 3.5f, cy + 5), Theme::Yellow, 1);
            fill(R(cx + 6, cy - 5, cx + 9.5f, cy + 5), Theme::Yellow, 1);
        }
        else                                  // نقطة حمرا بتنبض
        {
            const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(m_frame * 0.09f));
            b->SetColor(D2D1::ColorF(0xE5484D, 0.35f * pulse));
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x0 + cx + 5, y0 + cy), 8, 8), b);
            b->SetColor(D2D1::ColorF(0xFF5A5F, 0.6f + 0.4f * pulse));
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x0 + cx + 5, y0 + cy), 5, 5), b);
        }
        cx += 14;
    }
    if (s.recording)
    {
        text(Clock(s.elapsedMs), m_fTime.Get(), R(cx, 0, cx + 52, H),
             s.paused ? D2D1::ColorF(0xF5A524) : D2D1::ColorF(0xFFFFFF, 0.95f));
        cx += 52;
    }
    if (s.streaming)
    {
        const D2D1_RECT_F badge = R(cx + 4, 7, cx + 48, H - 7);
        fill(badge, D2D1::ColorF(0xE5484D, 0.9f), 4);
        text(L"مباشر", m_fBadge.Get(), badge, D2D1::ColorF(0xFFFFFF));
        cx += 52;
        // سرعة الرفع اللحظية جنب "مباشر"
        wchar_t up[48];
        swprintf_s(up, L"%.1f ميغابت/ث", s.uploadKbps / 1000.0);
        text(up, m_fBadge.Get(), R(cx, 0, cx + 92, H),
             s.uploadKbps > 0 ? D2D1::ColorF(0x3DD68C) : D2D1::ColorF(0xFFFFFF, 0.6f));
        cx += 96;
    }
    if (s.replay)       // الإعادة: أزرق = جاهزة ، أخضر "حُفظ" = حفظت مقطع
    {
        if (s.replaySaved)
        {
            const D2D1_RECT_F box = R(cx + 2, 4, cx + 62, H - 4);
            fill(box, D2D1::ColorF(0x3DD68C, 0.22f), 7);
            const wchar_t check[] = { 0xE73E, 0 };
            text(check, m_fIcon.Get(), D2D1::RectF(box.right - 22, y0, box.right - 4, y0 + H), D2D1::ColorF(0x3DD68C));
            text(L"حُفظ", m_fBadge.Get(), D2D1::RectF(box.left + 2, y0, box.right - 22, y0 + H), D2D1::ColorF(0x3DD68C));
            cx += 66;
        }
        else
        {
            const wchar_t replayGlyph[] = { 0xE81C, 0 };
            text(replayGlyph, m_fIcon.Get(), R(cx + 2, 0, cx + 26, H), D2D1::ColorF(0x4F8CFF, 0.95f));
            cx += 30;
        }
    }
    // المايك: أخضر لما تحكي، أبيض لما ساكت، أحمر مشطوب إذا مكتوم، باهت إذا مطفي
    const D2D1_RECT_F mic = R(cx + 2, 4, cx + 26, H - 4);
    const bool speaking = s.micOn && !s.micMuted && s.micLevel > 0.35f;
    if (speaking)
        fill(mic, D2D1::ColorF(0x3DD68C, 0.18f + 0.3f * std::min(1.0f, (s.micLevel - 0.35f) * 2)), 7);
    D2D1_COLOR_F mc = D2D1::ColorF(0xFFFFFF, 0.9f);
    if (!s.micOn)        mc = D2D1::ColorF(0xFFFFFF, 0.35f);
    else if (s.micMuted) mc = D2D1::ColorF(0xE5484D);
    else if (speaking)   mc = D2D1::ColorF(0x3DD68C);
    const wchar_t micGlyph[] = { (wchar_t)((!s.micOn || s.micMuted) ? 0xEC54 : 0xE720), 0 };
    text(micGlyph, m_fIcon.Get(), mic, mc);
}

// -------------------------------------------------------------
//  الإشعار: بطاقة غامقة، شريط ملوّن، أيقونة بدائرة، عنوان وشرح
// -------------------------------------------------------------
void RecordingOverlay::DrawToast(float x0, float y0, float alpha)
{
    ID2D1SolidColorBrush* b = m_brush.Get();
    auto col = [&](D2D1_COLOR_F c, float a) { c.a *= a * alpha; return c; };
    const D2D1_RECT_F card = D2D1::RectF(x0 + 0.5f, y0 + 0.5f, x0 + TOAST_W - 0.5f, y0 + TOAST_H - 0.5f);

    b->SetColor(col(D2D1::ColorF(0x000000), 0.25f));                 // ظل خفيف
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(card.left + 2, card.top + 4, card.right + 2, card.bottom + 5), 14, 14), b);
    b->SetColor(col(D2D1::ColorF(0x14171D), 0.94f));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(card, 14, 14), b);
    b->SetColor(col(m_toast.color, 0.55f));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(card, 14, 14), b, 1.2f);

    // شريط ملوّن على اليمين (الواجهة عربية)
    b->SetColor(col(m_toast.color, 1));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(card.right - 5, card.top + 14, card.right - 2, card.bottom - 14), 1.5f, 1.5f), b);

    // الأيقونة بدائرة
    const float icx = card.right - 36, icy = card.top + TOAST_H / 2;
    b->SetColor(col(m_toast.color, 0.20f));
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(icx, icy), 19, 19), b);
    const wchar_t glyph[] = { m_toast.glyph, 0 };
    b->SetColor(col(m_toast.color, 1));
    m_rt->DrawTextW(glyph, 1, m_fToastIcon.Get(), D2D1::RectF(icx - 19, icy - 19, icx + 19, icy + 19), b);

    // النصوص
    const float tr = card.right - 66, tl = card.left + 14;
    b->SetColor(col(D2D1::ColorF(0xFFFFFF), 0.97f));
    if (m_toast.sub.empty())
        m_rt->DrawTextW(m_toast.title.c_str(), (UINT32)m_toast.title.size(), m_fToastTitle.Get(),
                        D2D1::RectF(tl, card.top, tr, card.bottom), b);
    else
    {
        m_rt->DrawTextW(m_toast.title.c_str(), (UINT32)m_toast.title.size(), m_fToastTitle.Get(),
                        D2D1::RectF(tl, card.top + 11, tr, card.top + 35), b);
        b->SetColor(col(D2D1::ColorF(0xB4BAC6), 1));
        m_rt->DrawTextW(m_toast.sub.c_str(), (UINT32)m_toast.sub.size(), m_fToastSub.Get(),
                        D2D1::RectF(tl, card.top + 35, tr, card.top + 56), b);
    }
}

// -------------------------------------------------------------
//  الرسم: منرسم بصورة شفافة ومنعطيها لويندوز (UpdateLayeredWindow)
// -------------------------------------------------------------
void RecordingOverlay::Render(const OverlayState& s, int corner, bool showPill, float toastAlpha, float toastSlide)
{
    // على أي شاشة؟ اللي إنت عم تشتغل عليها (وإلا اللي عم تتسجّل ، وإلا الرئيسية)
    HMONITOR mon = UserMonitor(s.monitor);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    if (dpiX != m_dpi || !m_fTime)
    {
        m_dpi = dpiX;
        m_fTime       = MakeFormat(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, DWRITE_TEXT_ALIGNMENT_CENTER, L"Segoe UI");
        m_fBadge      = MakeFormat(11, DWRITE_FONT_WEIGHT_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fIcon       = MakeFormat(13, DWRITE_FONT_WEIGHT_NORMAL, false, DWRITE_TEXT_ALIGNMENT_CENTER, L"Segoe MDL2 Assets");
        m_fToastTitle = MakeFormat(15, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        m_fToastSub   = MakeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        m_fToastIcon  = MakeFormat(17, DWRITE_FONT_WEIGHT_NORMAL, false, DWRITE_TEXT_ALIGNMENT_CENTER, L"Segoe MDL2 Assets");
    }
    const float scale = m_dpi / 96.0f;

    // ---- الحجم: الشارة و/أو الإشعار، فوق بعض ----
    const bool right = (corner == 1 || corner == 3), top = (corner == 1 || corner == 2);
    const bool showToast = toastAlpha > 0;
    const float pillW = showPill ? PillWidth(s) : 0;
    const float slideMax = 40;                                  // قديش بيزحط الإشعار وقت ما يدخل من طرف الشاشة
    const float W  = std::max(pillW, showToast ? TOAST_W : 0.0f);
    const float Ht = (showPill ? H : 0) + (showPill && showToast ? GAP : 0) + (showToast ? TOAST_H + 6 : 0);
    const int pw = (int)std::ceil(W * scale), ph = (int)std::ceil(Ht * scale);

    // مكان العناصر جوّا الصورة (ملزوقين على طرف الزاوية)
    const float pillX = right ? W - pillW : 0;
    float pillY = 0, toastY = 0;
    if (top) { pillY = 0; toastY = showPill ? H + GAP : 0; }
    else     { toastY = 0; pillY = showToast ? TOAST_H + 6 + GAP : 0; }
    // الإشعار بيدخل من طرف الشاشة لجوّا (الجزء اللي برّا الصورة ما بيبيّن)
    const float toastX = right ? (W - TOAST_W) + slideMax * toastSlide : -slideMax * toastSlide;

    // ---- المكان على الشاشة ----
    const RECT& r = mi.rcMonitor;
    const int m = (int)(MARGIN * scale);
    const int x = right ? r.right - m - pw : r.left + m;
    const int y = top ? r.top + m : r.bottom - m - ph;

    HDC screen = GetDC(nullptr);
    HDC mem    = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = pw;
    bi.bmiHeader.biHeight = -ph;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = dib ? SelectObject(mem, dib) : nullptr;

    if (dib && !m_rt)
    {
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        m_factory->CreateDCRenderTarget(&props, &m_rt);
        m_brush.Reset();
    }
    if (dib && m_rt)
    {
        RECT rc{ 0, 0, pw, ph };
        m_rt->BindDC(mem, &rc);
        m_rt->SetDpi((float)m_dpi, (float)m_dpi);
        m_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);   // على خلفية شفافة
        if (!m_brush)
            m_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &m_brush);

        m_rt->BeginDraw();
        m_rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        if (showPill)
            DrawPill(s, pillX, pillY, pillW);
        if (showToast)
            DrawToast(toastX, toastY, toastAlpha);
        if (m_rt->EndDraw() == D2DERR_RECREATE_TARGET)
        {
            m_brush.Reset();
            m_rt.Reset();
        }

        POINT src{ 0, 0 }, dst{ x, y };
        SIZE  size{ pw, ph };
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(m_hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
    }

    if (old) SelectObject(mem, old);
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    // ويندوز أحياناً بيخبّيها (مثلاً "إظهار سطح المكتب" Win+D): منرجّعها
    if (!m_shown || !IsWindowVisible(m_hwnd) || IsIconic(m_hwnd))
    {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        m_shown = true;
    }
    // الألعاب والبرامج التانية (مثلاً اللي عليها "دائمًا في المقدمة") بتحاول تطلع فوق كلشي:
    // منرجّعها لفوق كل ربع ثانية ، وكل إطار وقت الإشعار ، وفورًا كل ما تبدّلت النافذة اللي قدامك
    if (m_frame % 8 == 0 || m_frame < 3 || m_fgChanged || toastAlpha > 0)
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    m_fgChanged = false;
}
