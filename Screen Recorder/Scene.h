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

// الانتقال بين المشاهد (تلاشي)
struct FadeInfo
{
    unsigned id = 0;          // بيزيد مع كل تبديل مشهد
    float    progress = 1;    // 0 = لسا بادئ ، 1 = خلص (ما في انتقال)
};

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

    // نفسها + حالة الانتقال
    void Snapshot(int& w, int& h, std::vector<SceneLayer>& out, FadeInfo& fade)
    {
        std::lock_guard<std::mutex> lock(mutex);
        w   = canvasW;
        h   = canvasH;
        out = layers;
        fade = FadeLocked();
    }

    // بلّش تلاشي (منناديها لما نبدّل المشهد). ms = 0 يعني تبديل فوري
    void StartFade(int ms)
    {
        std::lock_guard<std::mutex> lock(mutex);
        m_fadeId++;
        m_fadeMs    = ms;
        m_fadeStart = GetTickCount64();
    }

    // تبديل المشهد: طبقات جديدة + انتقال (بنفس اللحظة لحتى ما يفلت ولا إطار)
    void ReplaceLayers(const std::vector<SceneLayer>& newLayers, int fadeMs)
    {
        std::lock_guard<std::mutex> lock(mutex);
        layers = newLayers;
        m_fadeId++;
        m_fadeMs    = fadeMs;
        m_fadeStart = GetTickCount64();
    }

private:
    FadeInfo FadeLocked() const
    {
        FadeInfo f;
        f.id = m_fadeId;
        if (m_fadeMs > 0)
        {
            const double t = (double)(GetTickCount64() - m_fadeStart) / m_fadeMs;
            f.progress = t >= 1 ? 1.0f : (float)(t < 0 ? 0 : t);
            // منحنى ناعم (بيبلّش وبيخلص بهدوء)
            f.progress = f.progress * f.progress * (3 - 2 * f.progress);
        }
        return f;
    }

    unsigned  m_fadeId = 0;
    int       m_fadeMs = 0;
    ULONGLONG m_fadeStart = 0;
};

// إعدادات الرسم
struct ComposeParams
{
    BYTE*             out = nullptr;      // الصورة (BGRA)
    int               outW = 0, outH = 0, stride = 0;
    bool              bottomUp = false;   // السطور من تحت لفوق (Media Foundation)
    std::vector<int>* lostIds = nullptr;  // بيرجّع الطبقات اللي مصدرها ضاع
    FadeInfo          fade;
    // أي جزء من اللوحة منرسم (0 = اللوحة كلها). بالبث العمودي منقص من النص مثلاً
    double            viewX = 0, viewY = 0, viewW = 0, viewH = 0;
    // الطريق السريع (التسجيل على كرت الشاشة): ما منسحب الصورة للذاكرة،
    // منرجّع الـ texture نفسه (out بيضل فاضي)
    bool                      gpuOnly = false;
    struct ID3D11Texture2D**  outTexture = nullptr;
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
                 BYTE* out, int outW, int outH, int stride, bool bottomUp, std::vector<int>* lostIds = nullptr)
    {
        ComposeParams p;
        p.out = out; p.outW = outW; p.outH = outH; p.stride = stride; p.bottomUp = bottomUp; p.lostIds = lostIds;
        Compose(layers, canvasW, canvasH, p);
    }
    void Compose(const std::vector<SceneLayer>& layers, int canvasW, int canvasH, const ComposeParams& params);

    bool UsingGpu() const { return m_gpu != nullptr; }

    // بيشغّل محرّك كرت الشاشة هلق (بدل أول رسمة)، ومنه منوصل للجهاز (للتسجيل السريع)
    bool                 EnsureGpu();
    struct ID3D11Device* GpuDevice() const;

    // الإطارات المتغيّرة: في صورة جديدة من آخر مرة؟ (الطريقة القديمة ما بتعرف: دايماً نعم)
    bool PollNewFrames(const std::vector<SceneLayer>& layers);

private:
    void ComposeGdi(const std::vector<SceneLayer>& layers, int canvasW, int canvasH, const ComposeParams& p);
    void DrawMediaGdi(const SceneLayer& l, int x, int y, int w, int h, std::vector<int>* lostIds);

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

    // الانتقال: آخر صورة من المشهد القديم
    unsigned          m_fadeId = 0;
    bool              m_haveFrame = false;
    std::vector<BYTE> m_frozen;

    // صور الكاميرا/الصورة/النص (GDI)
    struct GdiMedia { unsigned long long version = 0; HBITMAP bmp = nullptr; void* bits = nullptr; int w = 0, h = 0; };
    std::map<int, GdiMedia> m_media;
};
