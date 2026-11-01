// =============================================================
//  Media.h - مصادر "الصور الجاهزة": الكاميرا، الصورة، النص
//
//  كل طبقة من هالأنواع بتعطي صورة BGRA بالذاكرة.
//  الكاميرا بتنفتح مرة وحدة بس حتى لو المعاينة والتسجيل والبث
//  كلهم عم يرسموها (الكاميرا ما بتقبل تنفتح مرتين).
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "Capture.h"

// صورة جاهزة (BGRA ، الألوان مضروبة بالشفافية "premultiplied")
struct MediaFrame
{
    int                  width = 0, height = 0;
    std::vector<uint8_t> pixels;          // width * height * 4 ، من فوق لتحت
    uint64_t             version = 0;     // بيتغيّر مع كل صورة جديدة
    ULONGLONG            tick = 0;        // إمتى وصلت (GetTickCount64) - للكاميرا وجهاز الالتقاط
};

struct CameraInfo
{
    std::wstring name;     // اسم الكاميرا
    std::wstring link;     // رقمها عند ويندوز
};

// كل الكاميرات الموصولة
std::vector<CameraInfo> ListCameras();

// آخر صورة لطبقة كاميرا/صورة/نص
// missing = true إذا الكاميرا مش موصولة أو الملف مش موجود
std::shared_ptr<const MediaFrame> GetLayerFrame(const SceneLayer& layer, bool* missing = nullptr);

// الحجم الطبيعي للطبقة (الصورة بحجمها ، النص حسب طوله ، الكاميرا حسب دقّتها)
bool MediaNaturalSize(const SceneLayer& layer, int& width, int& height);

// ---------- أجهزة الالتقاط ----------
// آخر صورة حيّة (بلا تأخير) من جهاز مفتوح هلق (بأي دقة). فاضي = الجهاز مش مفتوح
std::shared_ptr<const MediaFrame> PeekDeviceFrame(const std::wstring& link);
// الصورة سودا من أكتر من 3 ثواني؟ (غالباً HDCP شغّال بالبلايستيشن)
bool DeviceLooksBlack(const std::wstring& link);
// دقة وإطارات الجهاز الفعلية (بعد ما انفتح). false = لسا ما انفتح
bool DeviceActualFormat(const std::wstring& link, int& width, int& height, double& fps);

// تأخير الصورة (لمزامنة الترجمة): أجهزة الالتقاط ، والكاميرات
// كل الرسّامين (المعاينة ، التسجيل ، البث) بياخدوا الصورة المتأخرة نفسها
void SetMediaDelays(int deviceMs, int cameraMs);
int  MediaDelayMs(LayerKind kind);
