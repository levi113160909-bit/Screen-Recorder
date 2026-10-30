// =============================================================
//  Updater.h - التحديثات (من صفحة الإصدارات على GitHub)
//
//  1) الفحص: بيسأل GitHub عن آخر إصدار منشور (عند الفتح ، وكل 6 ساعات)
//  2) التنزيل بالخلفية: ملف المثبّت ScreenRecorder-Setup-<الإصدار>.exe من نفس الإصدار،
//     ومنتأكد إنو سليم ومش معدّل بالبصمة (SHA-256) اللي بيعطيها GitHub لكل ملف
//  3) التثبيت: عند فتح البرنامج المرة الجاية (أو فورًا من "تحديث الآن") منشغّل المثبّت
//     بالوضع الصامت (بلا نوافذ) ، وهو بيستبدل الملفات وبيعيد فتح البرنامج بـ --updated
//
//  النسخة المحمولة (zip): ما منثبّت لحالنا (ما منعرف وين حطّها المستخدم) ، بس
//  "تحديث الآن" بيفتح المثبّت العادي (التالي ← التالي) فبتصير نسخة مثبّتة.
//  ما في شي بيتثبّت وقت التسجيل أو البث.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <string>

// lParam = new UpdateInfo (الواجهة بتمسحه). wParam = 1 إذا الفحص طلبه المستخدم بإيده
constexpr UINT WM_UPDATE_RESULT = WM_APP + 43;
// التنزيل: wParam = 1 خلص (lParam = new std::wstring مسار الملف) ، 0 فشل (lParam = 0)
constexpr UINT WM_UPDATE_DOWNLOAD = WM_APP + 47;

struct UpdateInfo
{
    bool         ok = false;         // قدرنا نسأل؟
    bool         newer = false;      // في أحدث؟
    std::wstring version;            // "1.2.0"
    std::wstring url;                // صفحة الإصدار
    // ملف المثبّت بهالإصدار (فاضي = الإصدار ما فيه مثبّت)
    std::wstring setupName, setupUrl, setupSha256;   // البصمة: 64 حرف (hex) أو فاضي
    ULONGLONG    setupSize = 0;
};

// repo مثل L"levi113160909-bit/Screen-Recorder"
void CheckForUpdate(HWND notify, const std::wstring& repo, const std::wstring& currentVersion, bool manual);
bool IsNewerVersion(const std::wstring& latest, const std::wstring& current);

// تنزيل المثبّت بالخلفية لـ folder (بيبعت WM_UPDATE_DOWNLOAD لما يخلص)
void DownloadUpdate(HWND notify, const UpdateInfo& info, const std::wstring& folder);
extern std::atomic<int>  g_updateDownloadPercent;   // -1 = ما عم ننزّل ، 0..100
bool UpdateDownloading();

std::wstring UpdatesFolder();                        // %LOCALAPPDATA%\ScreenRecorder\Updates
void         ClearUpdatesFolder();
bool         FileSha256Matches(const std::wstring& path, const std::wstring& hex);

// البرنامج متثبّت بالمثبّت؟ (مش نسخة zip محمولة)
bool IsInstalledCopy();

// تشغيل المثبّت: silent = بلا نوافذ (وبيعيد فتح البرنامج بعدها) ، tray = يرجع يفتح مخفي جنب الساعة
bool RunSetup(const std::wstring& setupPath, bool silent, bool tray);
