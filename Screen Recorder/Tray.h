// =============================================================
//  Tray.h - أيقونة البرنامج جنب الساعة (منطقة الإشعارات)
//
//  لما تكبس ✕ البرنامج بيختفي بس بيضل شغّال هون:
//   - كبسة عالأيقونة = بيرجع يبيّن
//   - كليك يمين = قائمة (تسجيل، بث، إعادة، خروج)
//  والأيقونة بتتغيّر وقت التسجيل (حمرا) والإيقاف المؤقت.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>

// الرسالة اللي بتوصل للنافذة لما تكبس على الأيقونة
constexpr UINT WM_TRAY = WM_APP + 20;

class TrayIcon
{
public:
    void Add(HWND hwnd, HICON icon, const std::wstring& tip);
    void Remove();
    void SetIcon(HICON icon);
    void SetTip(const std::wstring& tip);
    void Balloon(const std::wstring& title, const std::wstring& text);   // إشعار صغير
    bool IsAdded() const { return m_added; }

private:
    NOTIFYICONDATAW m_nid{};
    bool            m_added = false;
};
