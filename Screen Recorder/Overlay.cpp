// =============================================================
//  Overlay.cpp - مؤشر التسجيل الصغير (شوف Overlay.h)
// =============================================================
#include "Overlay.h"
#include <shellscalingapi.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "shcore.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace
{
    const wchar_t OVERLAY_CLASS[] = L"ScreenRecorderOverlay";
    constexpr float H      = 32;      // ارتفاع الشارة (بالنقاط)
    constexpr float MARGIN = 18;      // بُعدها عن حافة الشاشة

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
    m_rt.Reset();
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_shown = false;
}

void RecordingOverlay::Update(const OverlayState& s, int corner, HWND owner)
{
    const bool active = s.recording || s.streaming || s.replay;
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

    if (corner <= 0 || !active || ownerInFront)
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
    if (m_frame % 2 == 0 || !m_shown || m_fgChanged)     // 15 مرة بالثانية بتكفّي (شي صغير)
        Render(s, corner);
}

// -------------------------------------------------------------
//  الرسم: منرسم بصورة شفافة ومنعطيها لويندوز (UpdateLayeredWindow)
// -------------------------------------------------------------
void RecordingOverlay::Render(const OverlayState& s, int corner)
{
    // على أي شاشة؟ اللي عم تتسجّل (main.cpp بيحددها من المشهد)، وإلا الرئيسية.
    // (قبل كنا نلحق النافذة اللي قدامك، فكان ينط بين الشاشات بشكل مزعج)
    HMONITOR mon = s.monitor ? s.monitor : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    if (dpiX != m_dpi || !m_fTime)
    {
        m_dpi = dpiX;
        m_fTime  = MakeFormat(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, DWRITE_TEXT_ALIGNMENT_CENTER, L"Segoe UI");
        m_fBadge = MakeFormat(11, DWRITE_FONT_WEIGHT_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fIcon  = MakeFormat(13, DWRITE_FONT_WEIGHT_NORMAL, false, DWRITE_TEXT_ALIGNMENT_CENTER, L"Segoe MDL2 Assets");
    }
    const float scale = m_dpi / 96.0f;

    // ---- العرض حسب المحتوى ----
    const std::wstring time = Clock(s.elapsedMs);
    const bool showDot    = s.recording || s.streaming;   // الإعادة لحالها: بلا نقطة حمرا (مش تسجيل حقيقي)
    const bool showTime   = s.recording;
    const bool showLive   = s.streaming;
    const bool showReplay = s.replay;
    float w = showDot ? 12.0f + 14.0f : 8.0f;                     // هامش + النقطة
    if (showTime)   w += 52;
    if (showLive)   w += 52;
    if (showReplay) w += s.replaySaved ? 66 : 30;
    w += 30 + 8;                                         // المايك + هامش
    const int pw = (int)std::ceil(w * scale), ph = (int)std::ceil(H * scale);

    // ---- المكان ----
    const RECT& r = mi.rcMonitor;
    const int m = (int)(MARGIN * scale);
    const bool right = (corner == 1 || corner == 3), top = (corner == 1 || corner == 2);
    const int x = right ? r.right - m - pw : r.left + m;
    const int y = top ? r.top + m : r.bottom - m - ph;

    // ---- لوحة رسم بالذاكرة ----
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
    }
    if (dib && m_rt)
    {
        RECT rc{ 0, 0, pw, ph };
        m_rt->BindDC(mem, &rc);
        m_rt->SetDpi((float)m_dpi, (float)m_dpi);
        m_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);   // على خلفية شفافة
        ComPtr<ID2D1SolidColorBrush> b;
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &b);
        auto fill = [&](const D2D1_RECT_F& rr, D2D1_COLOR_F c, float rad)
        {
            b->SetColor(c);
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(rr, rad, rad), b.Get());
        };
        auto text = [&](const std::wstring& t, IDWriteTextFormat* f, const D2D1_RECT_F& rr, D2D1_COLOR_F c)
        {
            b->SetColor(c);
            m_rt->DrawTextW(t.c_str(), (UINT32)t.size(), f, rr, b.Get());
        };

        m_rt->BeginDraw();
        m_rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        // الخلفية: كبسولة غامقة نص شفافة
        const D2D1_RECT_F pill = D2D1::RectF(0.5f, 0.5f, w - 0.5f, H - 0.5f);
        fill(pill, D2D1::ColorF(0x0E1014, 0.72f), H / 2);
        b->SetColor(D2D1::ColorF(0xFFFFFF, 0.10f));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(pill, H / 2, H / 2), b.Get(), 1);

        // من الشمال لليمين: النقطة، الوقت، "مباشر"، الإعادة، المايك
        float cx = showDot ? 12.0f : 8.0f;
        const float cy = H / 2;
        // النقطة: حمرا بتنبض (وقت الإيقاف المؤقت: صفرا وثابتة)
        if (!showDot)
            cx -= 14;          // ما في نقطة
        else if (s.paused && s.recording)
        {
            fill(D2D1::RectF(cx, cy - 5, cx + 3.5f, cy + 5), Theme::Yellow, 1);
            fill(D2D1::RectF(cx + 6, cy - 5, cx + 9.5f, cy + 5), Theme::Yellow, 1);
        }
        else
        {
            const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(m_frame * 0.09f));
            b->SetColor(D2D1::ColorF(0xE5484D, 0.35f * pulse));
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 5, cy), 8, 8), b.Get());
            b->SetColor(D2D1::ColorF(0xFF5A5F, 0.6f + 0.4f * pulse));
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 5, cy), 5, 5), b.Get());
        }
        cx += 14;

        if (showTime)
        {
            text(time, m_fTime.Get(), D2D1::RectF(cx, 0, cx + 52, H),
                 s.paused ? D2D1::ColorF(0xF5A524) : D2D1::ColorF(0xFFFFFF, 0.95f));
            cx += 52;
        }
        if (showLive)
        {
            const D2D1_RECT_F badge = D2D1::RectF(cx + 4, 7, cx + 48, H - 7);
            fill(badge, D2D1::ColorF(0xE5484D, 0.9f), 4);
            text(L"مباشر", m_fBadge.Get(), badge, D2D1::ColorF(0xFFFFFF));
            cx += 52;
        }

        // الإعادة: سهم دائري أزرق خفيف = "جاهزة"، وأخضر مع "حُفظ" لما تحفظ مقطع
        if (showReplay)
        {
            const wchar_t replayGlyph[] = { 0xE81C, 0 };
            if (s.replaySaved)
            {
                const D2D1_RECT_F box = D2D1::RectF(cx + 2, 4, cx + 62, H - 4);
                fill(box, D2D1::ColorF(0x3DD68C, 0.22f), 7);
                const wchar_t check[] = { 0xE73E, 0 };
                text(check, m_fIcon.Get(), D2D1::RectF(box.right - 22, 0, box.right - 4, H), D2D1::ColorF(0x3DD68C));
                text(L"حُفظ", m_fBadge.Get(), D2D1::RectF(box.left + 2, 0, box.right - 22, H), D2D1::ColorF(0x3DD68C));
                cx += 66;
            }
            else
            {
                text(replayGlyph, m_fIcon.Get(), D2D1::RectF(cx + 2, 0, cx + 26, H), D2D1::ColorF(0x4F8CFF, 0.95f));
                cx += 30;
            }
        }

        // المايك: أخضر لما تحكي، أبيض لما ساكت، أحمر مشطوب إذا مكتوم، باهت إذا مطفي
        const D2D1_RECT_F mic = D2D1::RectF(cx + 2, 4, cx + 26, H - 4);
        const bool speaking = s.micOn && !s.micMuted && s.micLevel > 0.35f;
        if (speaking)
            fill(mic, D2D1::ColorF(0x3DD68C, 0.18f + 0.3f * std::min(1.0f, (s.micLevel - 0.35f) * 2)), 7);
        D2D1_COLOR_F mc = D2D1::ColorF(0xFFFFFF, 0.9f);
        if (!s.micOn)        mc = D2D1::ColorF(0xFFFFFF, 0.35f);
        else if (s.micMuted) mc = D2D1::ColorF(0xE5484D);
        else if (speaking)   mc = D2D1::ColorF(0x3DD68C);
        const wchar_t micGlyph[] = { (wchar_t)((!s.micOn || s.micMuted) ? 0xEC54 : 0xE720), 0 };   // مايك مشطوب / مايك
        text(micGlyph, m_fIcon.Get(), mic, mc);

        m_rt->EndDraw();

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
    // الألعاب وسطح المكتب بيحاولوا يطلعوا فوق كلشي: كل ثانية، وكل ما تبدّلت النافذة، منرجع نحطها فوق
    if (m_frame % 30 == 0 || m_frame < 3 || m_fgChanged)
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_fgChanged = false;
}
