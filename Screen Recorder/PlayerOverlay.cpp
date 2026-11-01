// =============================================================
//  PlayerOverlay.cpp - الترجمة فوق اللعبة إلك إنت (شوف PlayerOverlay.h)
// =============================================================
#include "PlayerOverlay.h"
#include "Translator.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace
{
    const wchar_t CLASS_NAME[] = L"ScreenRecorderTranslationOverlay";

    struct OverlayWindow
    {
        HWND      hwnd = nullptr;
        uint64_t  version = 0;          // آخر صورة انعرضت
        RECT      rect{};               // وين انعرضت
        bool      shown = false;
        ULONGLONG lastTop = 0;          // آخر مرة رجّعناها لفوق
    };

    std::map<std::wstring, OverlayWindow> g_windows;     // حسب الملف الشخصي
    bool      g_blocked = false;
    ULONGLONG g_lastUpdate = 0;
    HWND      g_lastForeground = nullptr;

    LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCHITTEST)
            return HTTRANSPARENT;       // الماوس بيمرق من خلالها
        if (msg == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    HWND CreateOverlay()
    {
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc   = OverlayProc;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.lpszClassName = CLASS_NAME;
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassW(&wc);
            registered = true;
        }
        HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                    CLASS_NAME, L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd)
            return nullptr;
        // ضروري: إذا ويندوز ما قدر يخبّيها عن التصوير ، كانت رح تغطّي الإنكليزي عن قارئ النصوص
        // (فتختفي الجملة ، فتختفي الترجمة ، فيرجع يبيّن الإنكليزي... رجفة) وتطلع كمان بالبث
        if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE))
        {
            DestroyWindow(hwnd);
            g_blocked = true;
            return nullptr;
        }
        return hwnd;
    }

    void Hide(OverlayWindow& w)
    {
        if (w.hwnd && w.shown)
        {
            ShowWindow(w.hwnd, SW_HIDE);
            w.shown = false;
        }
    }

    void DestroyOverlay(OverlayWindow& w)
    {
        if (w.hwnd)
            DestroyWindow(w.hwnd);
        w = OverlayWindow{};
    }

    // مكان الترجمة على الشاشة (بكسل حقيقي)
    RECT TargetRect(const TranslateProfile& p)
    {
        RECT m = p.fromScreen ? p.monitor : p.overlayMonitor;
        if (m.right - m.left < 16 || m.bottom - m.top < 16)
        {
            MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
            m = mi.rcMonitor;
        }
        const double mw = m.right - m.left, mh = m.bottom - m.top;
        RECT r{};
        r.left   = m.left + (LONG)std::lround(p.rx * mw);
        r.top    = m.top + (LONG)std::lround(p.ry * mh);
        r.right  = r.left + std::max<LONG>(16, (LONG)std::lround(p.rw * mw));
        r.bottom = r.top + std::max<LONG>(16, (LONG)std::lround(p.rh * mh));
        return r;
    }

    // الصورة (BGRA مضروبة بالشفافية) -> النافذة
    void Present(OverlayWindow& w, const MediaFrame& f, const RECT& r)
    {
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = f.width;
        bmi.bmiHeader.biHeight      = -f.height;          // من فوق لتحت
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib && bits)
        {
            memcpy(bits, f.pixels.data(), (size_t)f.width * f.height * 4);
            HGDIOBJ old = SelectObject(mem, dib);
            POINT src{ 0, 0 }, dst{ r.left, r.top };
            SIZE  size{ f.width, f.height };
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            UpdateLayeredWindow(w.hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
            SelectObject(mem, old);
        }
        if (dib)
            DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }
}

void UpdatePlayerOverlays()
{
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastUpdate < 66)
        return;
    g_lastUpdate = now;
    const HWND fg = GetForegroundWindow();
    const bool fgChanged = fg != g_lastForeground;
    g_lastForeground = fg;

    const std::vector<TranslateProfile> profiles = TranslateProfiles();

    // الملفات اللي انطفى فيها الخيار (أو انحذفت): منسكّر نافذتها
    for (auto it = g_windows.begin(); it != g_windows.end();)
    {
        bool keep = false;
        for (const TranslateProfile& p : profiles)
            keep = keep || (p.id == it->first && p.playerOverlay);
        if (!keep || g_blocked)
        {
            DestroyOverlay(it->second);
            it = g_windows.erase(it);
        }
        else
            ++it;
    }
    if (g_blocked)
        return;

    for (const TranslateProfile& p : profiles)
    {
        if (!p.playerOverlay)
            continue;
        OverlayWindow& w = g_windows[p.id];

        // الجملة اللي ظاهرة هلق (وهالنداء بيخلّي القراءة شغّالة)
        std::wstring arabic;
        if (!TranslateLiveLine(p.id, arabic))
        {
            Hide(w);               // ما في جملة ، أو لسا عم تنترجم: منخلّيك تقرا الإنكليزي
            continue;
        }
        if (!w.hwnd && !(w.hwnd = CreateOverlay()))
            return;                // ويندوز ما بيدعم الإخفاء عن التصوير

        const RECT r = TargetRect(p);
        std::shared_ptr<const MediaFrame> f = RenderTranslationBox(arabic, r.right - r.left, r.bottom - r.top, p, false);
        if (!f || f->width < 1 || f->height < 1)
            continue;
        if (f->version != w.version || !EqualRect(&r, &w.rect))
        {
            Present(w, *f, r);
            w.version = f->version;
            w.rect    = r;
        }
        if (!w.shown || !IsWindowVisible(w.hwnd) || IsIconic(w.hwnd))
        {
            ShowWindow(w.hwnd, SW_SHOWNOACTIVATE);
            w.shown = true;
        }
        // الألعاب والبرامج التانية بتحاول تطلع فوق: منرجّعها لفوق كل ربع ثانية ، وفورًا لما تتبدّل النافذة اللي قدامك
        if (fgChanged || now - w.lastTop > 250)
        {
            SetWindowPos(w.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
            w.lastTop = now;
        }
    }
}

void ShutdownPlayerOverlays()
{
    for (auto& [id, w] : g_windows)
        DestroyOverlay(w);
    g_windows.clear();
}

bool PlayerOverlayBlocked()
{
    return g_blocked;
}
