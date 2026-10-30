// =============================================================
//  GameDetect.cpp - شوف GameDetect.h
// =============================================================
#include "GameDetect.h"
#include <algorithm>
#include <cwctype>

namespace
{
    std::wstring Lower(std::wstring s)
    {
        for (wchar_t& c : s) c = (wchar_t)std::towlower(c);
        return s;
    }

    std::wstring ExeOf(HWND hwnd)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!p)
            return L"";
        wchar_t buf[MAX_PATH * 2];
        DWORD len = MAX_PATH * 2;
        std::wstring out;
        if (QueryFullProcessImageNameW(p, 0, buf, &len))
            out.assign(buf, len);
        CloseHandle(p);
        return out;
    }

    // مجلدات الألعاب المعروفة
    bool InGameFolder(const std::wstring& lowerPath)
    {
        static const wchar_t* folders[] = {
            L"\\steamapps\\common\\", L"\\epic games\\", L"\\xboxgames\\",
            L"\\gog galaxy\\games\\", L"\\gog games\\", L"\\ea games\\", L"\\electronic arts\\",
            L"\\ubisoft game launcher\\games\\", L"\\riot games\\", L"\\battle.net\\", L"\\blizzard\\",
            L"\\rockstar games\\", L"\\games\\", L"\\amazon games\\library\\", L"\\itch\\apps\\"
        };
        for (const wchar_t* f : folders)
            if (lowerPath.find(f) != std::wstring::npos)
                return true;
        return false;
    }

    // برامج بتنفتح ملء الشاشة بس مش ألعاب
    bool NotAGame(const std::wstring& lowerExe)
    {
        static const wchar_t* names[] = {
            L"explorer.exe", L"chrome.exe", L"msedge.exe", L"firefox.exe", L"opera.exe", L"brave.exe", L"vivaldi.exe",
            L"vlc.exe", L"mpc-hc64.exe", L"mpc-be64.exe", L"potplayermini64.exe", L"wmplayer.exe", L"video.ui.exe",
            L"applicationframehost.exe", L"teams.exe", L"ms-teams.exe", L"zoom.exe", L"discord.exe", L"spotify.exe",
            L"obs64.exe", L"powerpnt.exe", L"winword.exe", L"excel.exe", L"devenv.exe", L"code.exe",
            L"steam.exe", L"steamwebhelper.exe", L"epicgameslauncher.exe", L"galaxyclient.exe", L"eadesktop.exe",
            L"battle.net.exe", L"riotclientux.exe", L"ubisoftconnect.exe", L"searchhost.exe", L"lockapp.exe",
            L"shellexperiencehost.exe", L"startmenuexperiencehost.exe", L"textinputhost.exe", L"claude.exe"
        };
        const size_t slash = lowerExe.find_last_of(L'\\');
        const std::wstring name = slash == std::wstring::npos ? lowerExe : lowerExe.substr(slash + 1);
        for (const wchar_t* n : names)
            if (name == n)
                return true;
        return false;
    }

    // النافذة كبيرة (نص الشاشة أو أكتر)؟ - لحتى ما نعتبر "لانشر" اللعبة الصغير لعبة
    bool Large(HWND hwnd)
    {
        RECT r;
        MONITORINFO mi{ sizeof(mi) };
        if (!GetWindowRect(hwnd, &r) || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
            return false;
        const double a = (double)(r.right - r.left) * (r.bottom - r.top);
        const double m = (double)(mi.rcMonitor.right - mi.rcMonitor.left) * (mi.rcMonitor.bottom - mi.rcMonitor.top);
        return a >= m * 0.5;
    }

    bool CoversMonitor(HWND hwnd)
    {
        RECT r;
        if (!GetWindowRect(hwnd, &r))
            return false;
        MONITORINFO mi{ sizeof(mi) };
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
            return false;
        return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
               r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
    }
}

bool DetectForegroundGame(HWND self, GameInfo& out)
{
    HWND fg = GetForegroundWindow();
    if (!fg || fg == self || !IsWindowVisible(fg) || IsIconic(fg))
        return false;
    fg = GetAncestor(fg, GA_ROOT);
    if (!fg || fg == self)
        return false;
    wchar_t cls[128] = {};
    GetClassNameW(fg, cls, 128);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd"))
        return false;                                       // سطح المكتب وشريط المهام

    const std::wstring exe = ExeOf(fg);
    if (exe.empty())
        return false;
    const std::wstring low = Lower(exe);
    if (NotAGame(low))
        return false;
    if (!(InGameFolder(low) && Large(fg)) && !CoversMonitor(fg))
        return false;

    out.hwnd = fg;
    out.exePath = exe;
    wchar_t title[256] = {};
    GetWindowTextW(fg, title, 256);
    out.title = title;
    if (out.title.empty())
    {
        const size_t slash = exe.find_last_of(L'\\'), dot = exe.find_last_of(L'.');
        out.title = exe.substr(slash + 1, dot > slash ? dot - slash - 1 : std::wstring::npos);
    }
    return true;
}
