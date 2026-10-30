// =============================================================
//  VirtualCam.h - "الكاميرا الافتراضية": المشهد كله ككاميرا ويب
//
//  بعد ما تشغّلها بيطلع "مسجّل الشاشة" بقائمة الكاميرات بديسكورد وزوم وتيمز
//  والمتصفح وكاميرا ويندوز... وبيشوفوا المشهد متل ما هو (الشاشة + الكاميرا + الصور + النص).
//
//  كيف؟ (ويندوز 11 بس - MFCreateVirtualCamera)
//   - VirtualCamera.dll (مشروع منفصل بنفس الحل) هي "مصدر الكاميرا" اللي ويندوز بيحمّلها
//     بخدمة Frame Server. لازم تتسجّل مرة وحدة بـ HKLM ، فأول مرة بيطلع طلب صلاحية المسؤول
//     (منحطّها بـ ProgramData لأن الخدمة ما بتقدر تقرأ من مجلد المستخدم)
//   - البرنامج بيرسم المشهد 30 مرة بالثانية بذاكرة مشتركة (VirtualCameraShared.h)
//     بس لما في برنامج فاتح الكاميرا فعلاً - غير هيك ما بياكل شي
//   - الكاميرا موجودة طول ما البرنامج فاتح وهي شغّالة؛ بتختفي لما توقفها أو تسكّر البرنامج
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

class Scene;

// wParam: 1 = اشتغلت ، 0 = فشلت (lParam = std::wstring* سبب الخطأ ، لازم ينحذف) ، 2 = توقفت
constexpr UINT WM_VCAM_RESULT = WM_APP + 45;

bool VirtualCamSupported();            // ويندوز 11؟
bool VirtualCamOn();                   // شغّالة
bool VirtualCamBusy();                 // عم تشتغل أو عم تتوقف
bool VirtualCamInUse();                // في برنامج عم يعرضها هلق
int  VirtualCamStateCode();            // 0 مطفية ، 1 عم تشتغل ، 2 شغّالة ، 3 عم تتوقف

void VirtualCamStart(HWND notify, Scene& scene);   // بالخلفية (ممكن يطلب صلاحية المسؤول أول مرة)
void VirtualCamStop();
void VirtualCamShutdown();             // عند إغلاق البرنامج (بيستنى شوي لحتى تنشال الكاميرا)

// نص الخطأ من lParam تبع WM_VCAM_RESULT (وبيحذفه)
std::wstring VirtualCamTakeError(LPARAM lParam);
