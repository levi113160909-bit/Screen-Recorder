// =============================================================
//  Gpu.h - محرّك التصوير الحديث (على كرت الشاشة)
//
//  بدل ما ننسخ الشاشة بالمعالج (GDI البطيئة)، منستعمل:
//   - Windows Graphics Capture: ويندوز بيعطينا صورة الشاشة/النافذة
//     مباشرة على كرت الشاشة (نفس طريقة OBS)
//   - Direct2D على Direct3D 11: منركّب الطبقات فوق بعض على كرت الشاشة
//  وبالآخر منسحب الصورة الجاهزة مرة وحدة للذاكرة لحتى نرمّزها.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <memory>
#include <vector>
#include "Capture.h"

class GpuCompositor
{
public:
    // بيرجّع nullptr إذا الجهاز ما بيدعم (ويندوز قديم مثلاً)
    static std::unique_ptr<GpuCompositor> Create(bool captureCursor);
    ~GpuCompositor();

    // بيركّب المشهد وبيكتب الصورة بـ out (BGRA، كل سطر stride بايت)
    // bottomUp = السطور من تحت لفوق (هيك بدها Media Foundation)
    // بيرجّع false إذا صار خطأ بكرت الشاشة (وقتها منرجع للطريقة القديمة)
    bool Compose(const std::vector<SceneLayer>& layers, int canvasW, int canvasH,
                 BYTE* out, int outW, int outH, int stride, bool bottomUp, std::vector<int>* lostIds);

private:
    GpuCompositor() = default;
    struct Impl;                    // التفاصيل مخبّاية بـ Gpu.cpp
    std::unique_ptr<Impl> m;
};
