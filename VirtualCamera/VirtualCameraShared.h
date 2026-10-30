// =============================================================
//  VirtualCameraShared.h - الذاكرة المشتركة بين البرنامج والكاميرا الافتراضية
//  مبني على VCamSample تبع Simon Mourier (رخصة MIT ، شوف LICENSE-VCamSample.txt)
//  والـ WIL من مايكروسوفت (رخصة MIT ، شوف wil/LICENSE.txt)
//
//  الكاميرا الافتراضية (VirtualCamera.dll) بتشتغل جوّا خدمة ويندوز "Frame Server"
//  (حساب Local Service بجلسة 0) ، مش جوّا برنامجنا. فمنبعتلها الصور بذاكرة مشتركة
//  باسم Global\... :
//   - الـ DLL هي اللي بتعمل الذاكرة (الخدمة عندها صلاحية Global ، برنامجنا لأ)
//     ومنعطي الكل صلاحية القراءة والكتابة
//   - البرنامج بيفتحها ويكتب الصور ، والـ DLL بتقرأ أحدث صورة لما برنامج (زوم ، ديسكورد...) يطلب
//   - 3 خانات للصور: منكتب بخانة ، ومنعلن عنها بعد ما تخلص (رقم تسلسلي فردي = عم تنكتب)
//   - heartbeat: الـ DLL بتكتب الوقت كل ما قرأت ، فالبرنامج بيعرف إذا حدا عم يستعمل الكاميرا
//     (وإذا لأ ما بيضيّع وقت برسم الصور)
// =============================================================
#pragma once
#include <windows.h>
#include <cstdint>

#define VCAM_MAPPING_NAME   L"Global\\ScreenRecorderVirtualCamera_v1"
#define VCAM_MAGIC          0x43565253u          // "SRVC"
#define VCAM_FRIENDLY_NAME  L"مسجّل الشاشة"

// {a319638e-7579-42dd-b59c-1430eabc002d}
#define VCAM_CLSID_STRING   L"{a319638e-7579-42dd-b59c-1430eabc002d}"
#define VCAM_CLSID_INIT     { 0xa319638e, 0x7579, 0x42dd, { 0xb5, 0x9c, 0x14, 0x30, 0xea, 0xbc, 0x00, 0x2d } }

constexpr uint32_t VCAM_OUT_W     = 1920;       // اللي بتشوفه البرامج
constexpr uint32_t VCAM_OUT_H     = 1080;
constexpr uint32_t VCAM_MAX_W     = 1920;       // أكبر صورة منبعتها
constexpr uint32_t VCAM_MAX_H     = 1080;
constexpr uint32_t VCAM_SLOTS     = 3;
constexpr size_t   VCAM_HEADER    = 4096;
constexpr size_t   VCAM_SLOT_SIZE = (size_t)VCAM_MAX_W * VCAM_MAX_H * 4;
constexpr size_t   VCAM_TOTAL     = VCAM_HEADER + VCAM_SLOTS * VCAM_SLOT_SIZE;

struct VCamSlot
{
    volatile LONG64 seq;        // فردي = عم تنكتب ، زوجي = جاهزة
    volatile LONG   width;      // حجم الصورة الفعلي (BGRA ، السطر = width * 4)
    volatile LONG   height;
    volatile LONG64 tick;       // GetTickCount64 وقت الكتابة
};

struct VCamHeader
{
    volatile LONG   magic;      // VCAM_MAGIC
    volatile LONG   version;    // 1
    volatile LONG64 heartbeat;  // آخر مرة الـ DLL قرأت (GetTickCount64)
    volatile LONG64 latest;     // رقم آخر صورة كاملة (الخانة = latest % VCAM_SLOTS)
    volatile LONG   mirror;     // محجوز
    volatile LONG   reserved;
    VCamSlot        slots[VCAM_SLOTS];
};

inline BYTE* VCamSlotData(void* base, uint32_t slot)
{
    return static_cast<BYTE*>(base) + VCAM_HEADER + (size_t)slot * VCAM_SLOT_SIZE;
}
