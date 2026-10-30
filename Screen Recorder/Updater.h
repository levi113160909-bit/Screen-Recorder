// =============================================================
//  Updater.h - "في إصدار جديد؟"
//
//  بيسأل GitHub عن آخر إصدار منشور (Releases) بالخلفية،
//  وإذا أحدث من النسخة اللي عندك بيخبّر الواجهة.
//  ما بينزّل ولا بيثبّت شي لحالو: بس بيقلّك وبيفتحلك الصفحة.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

// lParam = new UpdateInfo (الواجهة بتمسحه). wParam = 1 إذا الفحص طلبه المستخدم بإيده
constexpr UINT WM_UPDATE_RESULT = WM_APP + 43;

struct UpdateInfo
{
    bool         ok = false;         // قدرنا نسأل؟
    bool         newer = false;      // في أحدث؟
    std::wstring version;            // "1.2.0"
    std::wstring url;                // صفحة الإصدار
};

// repo مثل L"levi113160909-bit/Screen-Recorder"
void CheckForUpdate(HWND notify, const std::wstring& repo, const std::wstring& currentVersion, bool manual);
bool IsNewerVersion(const std::wstring& latest, const std::wstring& current);
