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
