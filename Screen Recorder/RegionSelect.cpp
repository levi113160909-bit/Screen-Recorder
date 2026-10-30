// =============================================================
//  RegionSelect.cpp - شوف RegionSelect.h
// =============================================================
#include "RegionSelect.h"
#include <algorithm>
#include <string>

#pragma comment(lib, "msimg32.lib")     // AlphaBlend

namespace
{
    struct State
    {
        RECT    virt{};                   // كل الشاشات سوا
        HDC     shotDc = nullptr;         // الشاشة متل ما هي
        HBITMAP shotBmp = nullptr;
        HDC     dimDc = nullptr;          // نفسها بس غامقة
        HBITMAP dimBmp = nullptr;
        bool    dragging = false, done = false, ok = false;
        POINT   a{}, b{};                 // بداية ونهاية السحب (إحداثيات الشاشة)
        HFONT   font = nullptr, fontSmall = nullptr;
    };
    State* g_state = nullptr;

    RECT Selection(const State& s)
    {
        RECT r{ std::min(s.a.x, s.b.x), std::min(s.a.y, s.b.y), std::max(s.a.x, s.b.x), std::max(s.a.y, s.b.y) };
        return r;
    }

    void DrawLabel(HDC dc, const std::wstring& text, RECT box, HFONT font, COLORREF bg, COLORREF fg)
    {
        HFONT old = (HFONT)SelectObject(dc, font);
        RECT calc = box;
        DrawTextW(dc, text.c_str(), -1, &calc, DT_CALCRECT | DT_SINGLELINE | DT_RTLREADING);
        const int w = calc.right - calc.left + 24, h = calc.bottom - calc.top + 12;
        RECT pill{ box.left, box.top, box.left + w, box.top + h };
        HBRUSH br = CreateSolidBrush(bg);
        HRGN rgn = CreateRoundRectRgn(pill.left, pill.top, pill.right + 1, pill.bottom + 1, 12, 12);
        FillRgn(dc, rgn, br);
        DeleteObject(rgn);
        DeleteObject(br);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, fg);
        DrawTextW(dc, text.c_str(), -1, &pill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_RTLREADING);
        SelectObject(dc, old);
    }

    void Paint(HWND hwnd)
    {
        State& s = *g_state;
        PAINTSTRUCT ps;
        HDC wdc = BeginPaint(hwnd, &ps);
        const int W = s.virt.right - s.virt.left, H = s.virt.bottom - s.virt.top;
        HDC dc = CreateCompatibleDC(wdc);
        HBITMAP bmp = CreateCompatibleBitmap(wdc, W, H);
        HGDIOBJ old = SelectObject(dc, bmp);

        BitBlt(dc, 0, 0, W, H, s.dimDc, 0, 0, SRCCOPY);
        if (s.dragging || s.done)
        {
            RECT r = Selection(s);
            OffsetRect(&r, -s.virt.left, -s.virt.top);
            BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, s.shotDc, r.left, r.top, SRCCOPY);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(0x4F, 0x8C, 0xFF));
            HGDIOBJ op = SelectObject(dc, pen);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, r.left - 1, r.top - 1, r.right + 1, r.bottom + 1);
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(pen);
            // الأبعاد
            const std::wstring size = std::to_wstring(r.right - r.left) + L" × " + std::to_wstring(r.bottom - r.top);
            RECT lab{ r.left, std::max(0L, r.top - 34L), r.left + 300, r.top };
            DrawLabel(dc, size, lab, s.fontSmall, RGB(0x4F, 0x8C, 0xFF), RGB(255, 255, 255));
        }
        // التعليمات فوق الشاشة اللي عليها الماوس
        POINT cur;
        GetCursorPos(&cur);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST), &mi);
        const std::wstring hint = L"اسحب لتحديد الجزء الذي تريد تصويره  •  Esc للإلغاء";
        HFONT of = (HFONT)SelectObject(dc, s.font);
        RECT calc{ 0, 0, 2000, 100 };
        DrawTextW(dc, hint.c_str(), -1, &calc, DT_CALCRECT | DT_SINGLELINE | DT_RTLREADING);
        SelectObject(dc, of);
        const int hw = calc.right + 24;
        const int cx = (mi.rcMonitor.left + mi.rcMonitor.right) / 2 - s.virt.left;
        RECT hb{ cx - hw / 2, mi.rcMonitor.top - s.virt.top + 24, cx + hw / 2, mi.rcMonitor.top - s.virt.top + 70 };
        DrawLabel(dc, hint, hb, s.font, RGB(0x16, 0x19, 0x1F), RGB(0xE8, 0xEA, 0xED));

        BitBlt(wdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
    }

    LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        State* s = g_state;
        switch (msg)
        {
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_LBUTTONDOWN:
            GetCursorPos(&s->a);
            s->b = s->a;
            s->dragging = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            if (s->dragging) GetCursorPos(&s->b);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (s->dragging)
            {
                GetCursorPos(&s->b);
                s->dragging = false;
                ReleaseCapture();
                const RECT r = Selection(*s);
                if (r.right - r.left >= 16 && r.bottom - r.top >= 16)   // صغيرة كتير = كبسة غلط
                {
                    s->ok = true;
                    s->done = true;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_RBUTTONUP:
            s->done = true;
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) s->done = true;
            return 0;
        case WM_PAINT:
            Paint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool SelectScreenRegion(HWND owner, RECT& out)
{
    State s;
    s.virt = { GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0 };
    s.virt.right  = s.virt.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    s.virt.bottom = s.virt.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int W = s.virt.right - s.virt.left, H = s.virt.bottom - s.virt.top;

    // منخبّي البرنامج لحتى تشوف اللي تحته
    const bool ownerVisible = owner && IsWindowVisible(owner);
    if (ownerVisible)
    {
        ShowWindow(owner, SW_HIDE);
        Sleep(250);
    }

    // صورة الشاشة + نسخة غامقة منها
    HDC screen = GetDC(nullptr);
    s.shotDc = CreateCompatibleDC(screen);
    s.shotBmp = CreateCompatibleBitmap(screen, W, H);
    SelectObject(s.shotDc, s.shotBmp);
    BitBlt(s.shotDc, 0, 0, W, H, screen, s.virt.left, s.virt.top, SRCCOPY | CAPTUREBLT);
    s.dimDc = CreateCompatibleDC(screen);
    s.dimBmp = CreateCompatibleBitmap(screen, W, H);
    SelectObject(s.dimDc, s.dimBmp);
    BitBlt(s.dimDc, 0, 0, W, H, s.shotDc, 0, 0, SRCCOPY);
    {
        HDC black = CreateCompatibleDC(screen);
        HBITMAP bb = CreateCompatibleBitmap(screen, 1, 1);
        SelectObject(black, bb);
        SetPixel(black, 0, 0, RGB(0, 0, 0));
        BLENDFUNCTION bf{ AC_SRC_OVER, 0, 150, 0 };
        AlphaBlend(s.dimDc, 0, 0, W, H, black, 0, 0, 1, 1, bf);
        DeleteObject(bb);
        DeleteDC(black);
    }
    ReleaseDC(nullptr, screen);
    s.font = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    s.fontSmall = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ScreenRecorderRegionSelect";
    RegisterClassW(&wc);
    g_state = &s;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP,
                                s.virt.left, s.virt.top, W, H, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd)
    {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        MSG msg;
        while (!s.done && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        DestroyWindow(hwnd);
    }
    g_state = nullptr;

    DeleteObject(s.font);
    DeleteObject(s.fontSmall);
    DeleteObject(s.shotBmp);
    DeleteDC(s.shotDc);
    DeleteObject(s.dimBmp);
    DeleteDC(s.dimDc);
    if (ownerVisible)
    {
        ShowWindow(owner, SW_SHOW);
        SetForegroundWindow(owner);
    }
    if (!s.ok)
        return false;
    out = Selection(s);
    return true;
}
