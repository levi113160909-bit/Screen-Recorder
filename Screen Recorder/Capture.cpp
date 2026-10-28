// =============================================================
//  Capture.cpp
// =============================================================
#include "Capture.h"
#include <dwmapi.h>
#include <cstdlib>
#include <cwchar>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace
{
    // حدود النافذة الظاهرة (بدون الإطار الشفاف حواليها)
    bool GetVisibleBounds(HWND hwnd, RECT& r)
    {
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        {
            if (!GetWindowRect(hwnd, &r))
                return false;
        }
        return r.right > r.left && r.bottom > r.top;
    }

    // منرسم سهم الماوس (الصورة العادية للشاشة ما بتحتويه)
    // src = مكان المصدر على الشاشة ، (x,y,w,h) = وين رسمناه على اللوحة
    void DrawCursor(HDC dc, const RECT& src, int x, int y, int w, int h)
    {
        CURSORINFO ci{};
        ci.cbSize = sizeof(ci);
        if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING))
            return;
        if (!PtInRect(&src, ci.ptScreenPos))
            return;                       // الماوس برّا المصدر

        ICONINFO ii{};
        if (!GetIconInfo(ci.hCursor, &ii))
            return;

        // حجم السهم الحقيقي
        BITMAP bm{};
        GetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm);
        const int cw = bm.bmWidth;
        const int ch = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;

        // إذا المصدر مصغّر/مكبّر منصغّر/منكبّر السهم معه
        const double sx = (double)w / (src.right - src.left);
        const double sy = (double)h / (src.bottom - src.top);
        const int px = x + (int)((ci.ptScreenPos.x - src.left - (int)ii.xHotspot) * sx);
        const int py = y + (int)((ci.ptScreenPos.y - src.top - (int)ii.yHotspot) * sy);
        DrawIconEx(dc, px, py, ci.hCursor, std::max(1, (int)(cw * sx)), std::max(1, (int)(ch * sy)), 0, nullptr, DI_NORMAL);

        if (ii.hbmMask)  DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
    }

    struct EnumState
    {
        std::vector<CaptureTarget>* list;
        HWND exclude;
        int  monitorCount;
    };

    BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param)
    {
        auto* state = (EnumState*)param;
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(monitor, &mi))
            return TRUE;

        ++state->monitorCount;
        const int w = mi.rcMonitor.right - mi.rcMonitor.left;
        const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

        CaptureTarget t;
        t.isMonitor = true;
        t.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        t.rect      = mi.rcMonitor;
        // \u202A ... \u202C = "اكتب هالجزء من اليسار لليمين" لحتى ما تنقلب الأرقام (2560×1440)
        t.name      = L"الشاشة " + std::to_wstring(state->monitorCount)
                    + L"  •  \u202A" + std::to_wstring(w) + L"×" + std::to_wstring(h) + L"\u202C";
        state->list->push_back(t);
        return TRUE;
    }

    BOOL CALLBACK AddWindow(HWND hwnd, LPARAM param)
    {
        auto* state = (EnumState*)param;

        // منتجاهل: برنامجنا، المخفي، النوافذ الفرعية، وأدوات النظام
        if (hwnd == state->exclude || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
            return TRUE;
        if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
            return TRUE;

        DWORD cloaked = 0;
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked)
            return TRUE;

        wchar_t cls[64] = {};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0)
            return TRUE;

        wchar_t title[256] = {};
        if (GetWindowTextW(hwnd, title, 256) == 0)
            return TRUE;

        CaptureTarget t;
        t.isMonitor = false;
        t.hwnd      = hwnd;
        t.name      = title;
        t.className = cls;

        // مسار البرنامج (متل chrome.exe)
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
        {
            wchar_t path[MAX_PATH] = {};
            DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(proc, 0, path, &len))
                t.exePath = path;
            CloseHandle(proc);
        }
        state->list->push_back(t);
        return TRUE;
    }
}

std::vector<CaptureTarget> ListCaptureTargets(HWND exclude)
{
    std::vector<CaptureTarget> list;
    EnumState state{ &list, exclude, 0 };
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, (LPARAM)&state);
    EnumWindows(AddWindow, (LPARAM)&state);
    return list;
}

HWND FindMatchingWindow(const CaptureTarget& saved, HWND exclude)
{
    const std::vector<CaptureTarget> all = ListCaptureTargets(exclude);
    // أول شي: نفس النوع ونفس العنوان بالضبط
    for (const CaptureTarget& t : all)
        if (!t.isMonitor && t.className == saved.className && t.name == saved.name)
            return t.hwnd;
    // بعدين: نفس النوع ونفس البرنامج (العنوان ممكن يتغيّر، متل تبويبات المتصفح)
    if (!saved.exePath.empty())
        for (const CaptureTarget& t : all)
            if (!t.isMonitor && t.className == saved.className && t.exePath == saved.exePath)
                return t.hwnd;
    return nullptr;
}

bool SameTarget(const CaptureTarget& a, const CaptureTarget& b)
{
    if (a.isMonitor != b.isMonitor)
        return false;
    return a.isMonitor ? EqualRect(&a.rect, &b.rect) != FALSE : a.hwnd == b.hwnd;
}

bool GetCaptureSize(const CaptureTarget& t, int& width, int& height)
{
    RECT r = t.rect;
    if (!t.isMonitor && (!IsWindow(t.hwnd) || !GetVisibleBounds(t.hwnd, r)))
        return false;
    width  = r.right - r.left;
    height = r.bottom - r.top;
    return width > 0 && height > 0;
}

UINT32 GetRefreshRate(const CaptureTarget& t)
{
    HMONITOR monitor = t.isMonitor ? MonitorFromRect(&t.rect, MONITOR_DEFAULTTONEAREST)
                                   : MonitorFromWindow(t.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return 60;

    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) || dm.dmDisplayFrequency <= 1)
        return 60;
    return dm.dmDisplayFrequency;
}

// =============================================================
FrameGrabber::FrameGrabber()
{
    m_screen = GetDC(nullptr);
    m_winDC  = CreateCompatibleDC(m_screen);
}

FrameGrabber::~FrameGrabber()
{
    if (m_winBmp)
    {
        SelectObject(m_winDC, m_oldBmp);
        DeleteObject(m_winBmp);
    }
    DeleteDC(m_winDC);
    ReleaseDC(nullptr, m_screen);
}

bool FrameGrabber::Grab(const CaptureTarget& t, HDC dst, int x, int y, int w, int h, bool drawCursor)
{
    if (w <= 0 || h <= 0)
        return true;
    SetStretchBltMode(dst, HALFTONE);     // تصغير ناعم بدل ما يطلع مكسّر
    SetBrushOrgEx(dst, 0, 0, nullptr);

    HDC  srcDC = nullptr;
    int  sx = 0, sy = 0;     // من وين منبلّش ننسخ
    RECT src{};              // مكان المصدر على الشاشة

    if (t.isMonitor)
    {
        // ---------- شاشة كاملة: منسخ من الشاشة مباشرة ----------
        src   = t.rect;
        srcDC = m_screen;
        sx    = src.left;
        sy    = src.top;
    }
    else
    {
        // ---------- نافذة ----------
        if (!IsWindow(t.hwnd))
            return false;              // انسكّرت

        RECT wr{}, vis{};
        const bool minimized = IsIconic(t.hwnd) != FALSE;
        if (!minimized && GetWindowRect(t.hwnd, &wr) && GetVisibleBounds(t.hwnd, vis))
        {
            const int ww = wr.right - wr.left;
            const int wh = wr.bottom - wr.top;
            if (ww != m_winW || wh != m_winH)     // تغيّر حجمها: منجهّز لوحة جديدة
            {
                if (m_winBmp)
                {
                    SelectObject(m_winDC, m_oldBmp);
                    DeleteObject(m_winBmp);
                }
                m_winBmp = CreateCompatibleBitmap(m_screen, ww, wh);
                m_oldBmp = SelectObject(m_winDC, m_winBmp);
                m_winW = ww;
                m_winH = wh;
            }
            // منطلب من النافذة ترسم حالها عندنا (حتى لو في شي فوقها)
            PrintWindow(t.hwnd, m_winDC, PW_RENDERFULLCONTENT);
            m_lastVis = vis;
            m_lastOx  = vis.left - wr.left;
            m_lastOy  = vis.top - wr.top;
        }
        if (!m_winBmp)
            return true;               // لسا ما عنا ولا صورة منها
        // إذا مصغّرة منستعمل آخر صورة أخدناها
        src   = m_lastVis;
        srcDC = m_winDC;
        sx    = m_lastOx;
        sy    = m_lastOy;
        drawCursor = drawCursor && !minimized;
    }

    const int sw = src.right - src.left;
    const int sh = src.bottom - src.top;
    if (sw <= 0 || sh <= 0)
        return true;

    if (std::abs(sw - w) <= 1 && std::abs(sh - h) <= 1)
        BitBlt(dst, x, y, w, h, srcDC, sx, sy, SRCCOPY);
    else
        StretchBlt(dst, x, y, w, h, srcDC, sx, sy, sw, sh, SRCCOPY);

    if (drawCursor)
        DrawCursor(dst, src, x, y, w, h);
    return true;
}
