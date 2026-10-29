// =============================================================
//  Recorder.h - "المسجّل": بيجمع الصورة والصوت وبيكتبهم بملف MP4
//
//  في خيطين بيشتغلوا سوا:
//   - خيط الصورة: 30 مرة بالثانية بياخد صورة وبيكتبها (H.264)
//   - خيط الصوت : بيخلط صوت الكمبيوتر والمايك وبيكتبه (AAC)
//
//  مسارات الصوت المنفصلة (للمونتاج):
//   المسار 1 = كل الأصوات مخلوطة (هاد اللي بتسمعه بأي مشغّل)
//   وبعده مسار لكل مصدر لحالو، بنفس ترتيب RecordOptions::audio
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX      // بيمنع ويندوز يخرّب std::min و std::max
#endif

#include <windows.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Scene.h"
#include "Audio.h"

// رسالة بيبعتها المسجّل للواجهة إذا وقف لحالو (صار خطأ)
// wParam = النتيجة (HRESULT)
constexpr UINT WM_RECORDER_DONE = WM_APP + 1;

// نوع المشفّر (اللي بيضغط الفيديو)
enum class EncoderMode
{
    Auto,       // جرّب كرت الشاشة، وإذا ما زبط المعالج
    Hardware,   // كرت الشاشة (أسرع وأخف عالجهاز)
    Software    // المعالج (أبطأ بس بيشتغل بكل مكان)
};

struct RecordOptions
{
    Scene*                    scene = nullptr;     // المشهد اللي منسجّله (بيقدر يتغيّر وقت التسجيل)
    std::wstring              path;
    std::vector<AudioSource*> audio;      // مصادر الصوت الشغّالة
    HWND                      notify  = nullptr;
    UINT32                    fps     = 30;            // عدد الإطارات بالثانية
    UINT32                    bitrate = 8'000'000;     // معدّل البت (بت بالثانية)
    EncoderMode               encoder = EncoderMode::Auto;
    bool                      crashSafe = true;    // MP4 مقطّع: ما بيخرب إذا البرنامج ضرب
    bool                      separateTracks = false;   // مسار لكل مصدر صوت (بالإضافة للمخلوط)
};

class Recorder
{
public:
    ~Recorder() { Stop(); }

    HRESULT Start(const RecordOptions& options);
    HRESULT Stop();                       // بيوقف وبيحفظ الملف، وبيرجّع النتيجة

    void SetPaused(bool paused);
    bool IsRecording() const { return m_active; }
    bool IsPaused() const    { return m_paused; }
    LONGLONG ElapsedMs()     { return Elapsed100ns() / 10'000; }

    // اسم المشفّر اللي عم يشتغل فعلياً (بعد Start)
    std::wstring EncoderName() const { return m_encoderName; }

    // هل التصوير شغّال على كرت الشاشة (الطريقة الحديثة)؟
    bool UsingGpuCapture() const { return m_usingGpu; }

    // كل مشفّرات H.264 الموجودة بالجهاز
    struct EncoderInfo { std::wstring name; bool hardware; };
    static std::vector<EncoderInfo> ListEncoders();

private:
    void     VideoThread();
    void     AudioThread();
    LONGLONG Elapsed100ns();              // الوقت المسجّل (بدون فترات الإيقاف المؤقت)
    HRESULT  WriteVideo(const void* pixels, LONGLONG time);
    HRESULT  WriteAudio(DWORD stream, const float* pcm, size_t frames, LONGLONG time);

    RecordOptions m_opt;
    std::vector<int> m_taps;              // مخازن الصوت تبعنا (وحدة لكل مصدر)
    int           m_width = 0, m_height = 0;
    UINT32        m_fps = 30;
    LONGLONG      m_frameDuration = 10'000'000 / 30;
    std::wstring  m_encoderName;

    Microsoft::WRL::ComPtr<IMFSinkWriter> m_writer;
    std::mutex    m_writeMutex;            // ما بيكتبوا الخيطين بنفس اللحظة
    DWORD         m_videoStream = 0;
    std::vector<DWORD> m_audioStreams;     // [0] = المخلوط ، وبعده مسار لكل مصدر (إذا مفعّل)
    bool          m_hasAudio = false;

    std::atomic<bool> m_active{ false };
    std::atomic<bool> m_usingGpu{ false };
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_paused{ false };
    HRESULT           m_error = S_OK;
    std::thread       m_videoThread, m_audioThread;

    // الساعة
    std::mutex m_clockMutex;
    LONGLONG   m_freq = 1, m_start = 0, m_pauseStart = 0, m_pausedTotal = 0;
};
