// =============================================================
//  Scene.h - "المشهد" متل OBS
//
//  المشهد = لوحة بحجم الفيديو (مثلاً 1920×1080)
//  وفوقها طبقات: كل طبقة مصدر (شاشة أو نافذة) إلها مكان وحجم.
//  الطبقة الأولى بالقائمة تحت، والأخيرة فوق الكل.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "Capture.h"

// (SceneLayer معرّفة بـ Capture.h)

class Scene
{
public:
    std::mutex              mutex;               // الواجهة وخيط التسجيل بيوصلوا سوا، فلازم قفل
    int                     canvasW = 1920, canvasH = 1080;
    std::vector<SceneLayer> layers;
    int                     nextId = 1;

    // نسخة سريعة من المشهد (لحتى ما نمسك القفل وقت الرسم)
    void Snapshot(int& w, int& h, std::vector<SceneLayer>& out)
    {
        std::lock_guard<std::mutex> lock(mutex);
        w   = canvasW;
        h   = canvasH;
        out = layers;
    }
};

// -------------------------------------------------------------
// Compositor: بيرسم كل الطبقات فوق بعض على لوحة وحدة
//   - أول شي بيجرّب كرت الشاشة (GpuCompositor) - الطريقة الحديثة
//   - إذا ما زبط بيرجع للطريقة القديمة (GDI) لحتى ما يوقف البرنامج
// -------------------------------------------------------------
class GpuCompositor;

class Compositor
{
public:
    explicit Compositor(bool drawCursor);
    ~Compositor();

    // بيكتب الصورة بـ out (BGRA)، كل سطر stride بايت
    // bottomUp = السطور من تحت لفوق (Media Foundation) ، غير هيك من فوق لتحت (Direct2D)
    // lostIds: بيرجّع أرقام الطبقات اللي نوافذها انسكّرت
    void Compose(const std::vector<SceneLayer>& layers, int canvasW, int canvasH,
                 BYTE* out, int outW, int outH, int stride, bool bottomUp, std::vector<int>* lostIds = nullptr);

    bool UsingGpu() const { return m_gpu != nullptr; }

private:
    void ComposeGdi(const std::vector<SceneLayer>& layers, int canvasW, int canvasH,
                    BYTE* out, int outW, int outH, int stride, bool bottomUp, std::vector<int>* lostIds);

    bool m_cursor;
    bool m_gpuTried = false;
    std::unique_ptr<GpuCompositor> m_gpu;

    // الطريقة القديمة (احتياط)
    std::map<int, std::unique_ptr<FrameGrabber>> m_grabbers;
    HDC     m_dc = nullptr;
    HBITMAP m_dib = nullptr;
    HGDIOBJ m_oldDib = nullptr;
    void*   m_bits = nullptr;
    int     m_dibW = 0, m_dibH = 0;
};
