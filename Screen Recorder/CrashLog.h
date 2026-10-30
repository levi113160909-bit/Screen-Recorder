// =============================================================
//  CrashLog.h - سجل الأعطال
//
//  إذا البرنامج وقع (أي خطأ ما انمسك، بأي خيط) منكتب بـ:
//      %LOCALAPPDATA%\ScreenRecorder\Crashes\
//  ملفين بنفس الاسم (crash_السنة-الشهر-اليوم_الساعة-الدقيقة-الثانية):
//    - .txt : مقروء: الإصدار، ويندوز، نوع الخطأ، ومكانه (ومسار الاستدعاءات)
//    - .dmp : صورة عن ذاكرة البرنامج لحظة الوقوع (بتنفتح بـ Visual Studio للتحليل)
//  منخلّي آخر 10 تقارير بس. وبالتشغيل الجاي البرنامج بيقلّك إنو صار عطل.
//
//  للتجربة: شغّل البرنامج بـ --test-crash (بيوقع عن قصد بعد ما يفتح)
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

// رسالة: وقوع عن قصد (للتجربة بس ، من --test-crash)
constexpr UINT WM_TEST_CRASH = WM_APP + 46;

// أول شي بـ wWinMain
void InstallCrashHandler(const wchar_t* appVersion);

// صار عطل بالتشغيل الماضي؟ (بيرجّع true مرة وحدة ، وبيعطيك مسار التقرير)
bool TakePendingCrash(std::wstring& reportPath);

// مجلد التقارير (حتى لو لسا ما انعمل)
std::wstring CrashFolder();

// وقوع عن قصد (للتجربة)
[[noreturn]] void TestCrash();
