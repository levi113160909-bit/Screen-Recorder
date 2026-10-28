// =============================================================
//  Streamer.h - البث المباشر لأكتر من منصة بنفس الوقت
//
//  الفكرة: منضغط الفيديو والصوت "مرة وحدة بس"،
//  وبعدين منبعت نفس القطع لكل المنصات (يوتيوب + تيك توك + ...).
//  هيك الجهاز ما بيتعب أكتر إذا زدت منصات، بس النت بيتقسم.
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

// رسالة للواجهة إذا البث وقف لحالو (مشكلة بالمشفّر)
constexpr UINT WM_STREAMER_DONE = WM_APP + 2;

struct StreamOptions
{
    Scene*                    scene = nullptr;
    std::vector<AudioSource*> audio;
    std::vector<StreamTarget> targets;     // الوجهات المفعّلة بس
    HWND                      notify = nullptr;
    UINT32                    width = 1920, height = 1080;
    UINT32                    fps = 30;
    UINT32                    videoKbps = 6000;
    UINT32                    audioKbps = 160;
    EncoderMode               encoder = EncoderMode::Auto;
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
    void VideoThread();
    void AudioThread();
    LONGLONG Elapsed100ns();
    void Broadcast(std::vector<EncodedPacket>& packets);

    // أشيا مشتركة مع المُرسِلات (بتضل عايشة حتى لو المُرسِل لسا عم يسكّر)
    struct Shared
    {
        std::mutex           mutex;
        std::vector<uint8_t> avcConfig;
        std::atomic<bool>    forceKeyframe{ true };
    };

    StreamOptions m_opt;
    std::shared_ptr<Shared> m_shared;
    std::vector<std::unique_ptr<RtmpOutput>> m_outputs;
    std::vector<int> m_taps;

    H264Encoder  m_video;
    AacEncoder   m_audio;
    std::wstring m_encoderName;
    UINT32       m_width = 0, m_height = 0;
    LONGLONG     m_frameDuration = 10'000'000 / 30;

    std::atomic<bool> m_active{ false };
    std::atomic<bool> m_running{ false };
    std::thread       m_videoThread, m_audioThread;

    LONGLONG m_freq = 1, m_start = 0;
};
