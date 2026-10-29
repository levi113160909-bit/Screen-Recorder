// =============================================================
//  Tray.cpp - أيقونة البرنامج جنب الساعة (شوف Tray.h)
// =============================================================
#include "Tray.h"
#include <cwchar>

void TrayIcon::Add(HWND hwnd, HICON icon, const std::wstring& tip)
{
    m_nid = {};
    m_nid.cbSize           = sizeof(m_nid);
    m_nid.hWnd             = hwnd;
    m_nid.uID              = 1;
    m_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    m_nid.uCallbackMessage = WM_TRAY;
    m_nid.hIcon            = icon;
    wcsncpy_s(m_nid.szTip, tip.c_str(), _TRUNCATE);
    m_added = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    if (m_added)
    {
        m_nid.uVersion = NOTIFYICON_VERSION_4;    // الطريقة الحديثة (كليك يمين = WM_CONTEXTMENU)
        Shell_NotifyIconW(NIM_SETVERSION, &m_nid);
    }
}

void TrayIcon::Remove()
{
    if (m_added)
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
    m_added = false;
}

void TrayIcon::SetIcon(HICON icon)
{
    if (!m_added || m_nid.hIcon == icon)
        return;
    m_nid.hIcon  = icon;
    m_nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::SetTip(const std::wstring& tip)
{
    if (!m_added || tip == m_nid.szTip)
        return;
    wcsncpy_s(m_nid.szTip, tip.c_str(), _TRUNCATE);
    m_nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::Balloon(const std::wstring& title, const std::wstring& text)
{
    if (!m_added)
        return;
    m_nid.uFlags      = NIF_INFO;
    m_nid.dwInfoFlags = NIIF_USER;                     // بأيقونة البرنامج
    m_nid.hBalloonIcon = m_nid.hIcon;
    wcsncpy_s(m_nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(m_nid.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    m_nid.szInfo[0] = 0;
}
