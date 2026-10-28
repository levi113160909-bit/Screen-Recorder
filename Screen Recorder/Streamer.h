// =============================================================
//  Streamer.h - البث المباشر لأكتر من منصة بنفس الوقت (+ الإعادة)
//
//  الفكرة: منضغط الفيديو والصوت "مرة وحدة بس"،
//  وبعدين منبعت نفس القطع لكل المنصات (يوتيوب + تيك توك + ...).
//  إذا في منصة عمودية (تيك توك) منضغط نسخة عمودية تانية من نفس المشهد.
//  ونفس المحرّك بيشغّل "الإعادة": بيحفظ آخر ثواني بالذاكرة.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Recorder.h"      // EncoderMode
#include "Rtmp.h"
#include "Replay.h"

// رسالة للواجهة إذا البث وقف لحالو (مشكلة بالمشفّر)
// lParam = 1 إذا كان محرّك الإعادة
constexpr UINT WM_STREAMER_DONE = WM_APP + 2;

// طريقة النسخة العمودية
enum class VerticalMode
{
    Crop,    // قصّ من نص الشاشة (الصورة كبيرة وواضحة)
    Fit      // الشاشة كاملة بالنص وفوقها وتحتها أسود
};

struct StreamOptions
{
    Scene*                    scene = nullptr;
    std::vector<AudioSource*> audio;
    std::vector<StreamTarget> targets;     // الوجهات المفعّلة بس
    HWND                      notify = nullptr;
    UINT32                    width = 1920, height = 1080;            // الأفقي
    UINT32                    verticalW = 1080, verticalH = 1920;     // العمودي
    VerticalMode              verticalMode = VerticalMode::Crop;
    UINT32                    fps = 30;
    UINT32                    videoKbps = 6000;
    UINT32                    audioKbps = 160;
    EncoderMode               encoder = EncoderMode::Auto;
    std::shared_ptr<ReplayBuffer> replay;   // إذا موجود: منحفظ آخر ثواني فيه
    bool                      isReplay = false;
};

class Streamer
{
public:
    ~Streamer() { Stop(); }

    // error = سبب الفشل بالعربي
    bool Start(const StreamOptions& options, std::wstring& error);
    void Stop();

    bool IsActive() const { return m_active; }
    LONGLONG ElapsedMs();
    std::wstring EncoderName() const { return m_encoderName; }

    // حالة كل وجهة (بنفس ترتيب options.targets)
    std::vector<OutputStatus> Status();
    std::vector<StreamTarget> Targets() const { return m_opt.targets; }

private:
    void VideoThread(int track);
    void AudioThread();
    LONGLONG Elapsed100ns();
    void Broadcast(std::vector<EncodedPacket>& packets);

    // أشيا مشتركة مع المُرسِلات (بتضل عايشة حتى لو المُرسِل لسا عم يسكّر)
    struct Shared
    {
        std::mutex           mutex;
        std::vector<uint8_t> avcConfig[2];
        std::atomic<bool>    forceKeyframe[2];
        Shared() { forceKeyframe[0] = true; forceKeyframe[1] = true; }
    };

    struct Track
    {
        bool        active = false;
        UINT32      width = 0, height = 0;
        H264Encoder encoder;
        std::thread thread;
    };

    StreamOptions m_opt;
    std::shared_ptr<Shared> m_shared;
    std::vector<std::unique_ptr<RtmpOutput>> m_outputs;
    std::vector<int> m_outputTrack;
    std::vector<int> m_taps;

    Track        m_tracks[2];
    AacEncoder   m_audio;
    std::mutex   m_broadcastMutex;          // الخيطين بيبعتوا لنفس المُرسِلات
    std::wstring m_encoderName;
    LONGLONG     m_frameDuration = 10'000'000 / 30;

    std::atomic<bool> m_active{ false };
    std::atomic<bool> m_running{ false };
    std::thread       m_audioThread;

    LONGLONG m_freq = 1, m_start = 0;
};
