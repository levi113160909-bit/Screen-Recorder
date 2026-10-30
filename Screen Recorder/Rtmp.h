// =============================================================
//  Rtmp.h - إرسال البث لمنصة وحدة (يوتيوب، تويتش، تيك توك...)
//
//  RTMP = اللغة اللي بتحكيها كل منصات البث تقريباً.
//  كل وجهة إلها "مُرسِل" خاص فيه بخيط لحالو، فإذا وحدة وقعت
//  الباقي بيكمّلوا، وهي بتحاول ترجع تتصل لحالها.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Encoders.h"

class Transport;

// المنصات الجاهزة
enum class Platform { YouTube, Twitch, Facebook, Kick, TikTok, Custom };

struct StreamTarget
{
    std::wstring name;                 // الاسم اللي بيبيّن
    Platform     platform = Platform::Custom;
    std::wstring url;                  // عنوان السيرفر (rtmp:// أو rtmps://)
    std::wstring key;                  // مفتاح البث (سرّي!)
    bool         enabled = true;
    bool         vertical = false;     // بث عمودي 9:16 (تيك توك)
    int          codec = 0;            // 0 = H.264 ، 1 = HEVC (يوتيوب)
    UINT32       kbps = 0;             // معدّل البت لهالمنصة (0 = الإعداد العام للبث)
};

// المنصة بتستقبل HEVC؟ (يوتيوب رسمياً ، و"مخصّص" على مسؤولية المستخدم)
inline bool PlatformSupportsHevc(Platform p) { return p == Platform::YouTube || p == Platform::Custom; }

enum class OutputState { Idle, Connecting, Live, Reconnecting, Failed };

struct OutputStatus
{
    OutputState  state = OutputState::Idle;
    std::wstring message;              // سبب الخطأ إذا في
    double       kbps = 0;             // سرعة الإرسال الحالية
    uint32_t     dropped = 0;          // كم قطعة انرمت بسبب بطء النت
};

// معلومات الفيديو للمنصة
struct StreamInfo
{
    UINT32 width = 1920, height = 1080, fps = 30, videoKbps = 6000, audioKbps = 160;
    bool   hevc = false;                // HEVC عبر Enhanced RTMP (يوتيوب) ، وإلا H.264
};

class RtmpOutput
{
public:
    using ConfigGetter = std::function<std::vector<uint8_t>()>;   // بيرجّع إعدادات الفيديو (SPS/PPS)

    RtmpOutput(const StreamTarget& target, const StreamInfo& info, ConfigGetter avcConfig,
               std::function<void()> requestKeyframe);
    ~RtmpOutput();

    void Start();
    void Stop();
    void Push(const std::shared_ptr<const EncodedPacket>& pkt);
    OutputStatus Status();
    // كم ثانية من القطع ناطرة تنبعت (إذا عم تكبر: النت أبطأ من البث)
    double BufferedSeconds();

private:
    void ThreadMain();
    bool Session(std::wstring& error);      // اتصال كامل: بيرجع لما ينقطع أو نوقّف

    // RTMP
    bool Handshake();
    bool SendChunkSize(uint32_t size);
    bool SendRtmp(uint8_t csid, uint8_t type, uint32_t ts, uint32_t streamId, const uint8_t* data, size_t len);
    bool SendCommand(uint8_t csid, uint32_t streamId, const std::vector<uint8_t>& amf);
    struct Message { uint8_t type = 0; uint32_t streamId = 0; std::vector<uint8_t> data; };
    int  ParseChunk(Message& msg, bool& complete);  // 1 = قرينا قطعة ، 0 = ناقص بيانات ، -1 = خطأ
    bool ReadMessage(Message& msg, int timeoutMs);
    bool HandleControl(const Message& msg);         // رسائل التحكم (حجم القطع، ping...)
    bool SendAckIfNeeded();
    bool PollIncoming();
    bool WaitForResult(double txn, double* number, std::wstring& error, int timeoutMs);
    bool WaitForPublish(std::wstring& error, int timeoutMs);
    bool SendPacket(const EncodedPacket& pkt);

    void SetState(OutputState s, const std::wstring& message = L"");

    StreamTarget  m_target;
    StreamInfo    m_info;
    ConfigGetter  m_getConfig;
    std::function<void()> m_requestKeyframe;

    std::unique_ptr<Transport> m_t;
    std::atomic<bool> m_running{ false };
    std::thread       m_thread;

    // الطابور: القطع اللي ناطرة تنبعت
    std::mutex              m_qMutex;
    std::condition_variable m_qCv;
    std::deque<std::shared_ptr<const EncodedPacket>> m_queue;
    std::atomic<bool>       m_live{ false };
    std::atomic<bool>       m_waitKeyframe{ true };
    std::atomic<uint32_t>   m_dropped{ 0 };

    // حالة الاتصال
    std::mutex   m_statusMutex;
    OutputStatus m_status;

    // RTMP
    uint32_t m_outChunk = 128, m_inChunk = 128;
    uint32_t m_streamId = 1;
    uint64_t m_received = 0, m_lastAck = 0, m_window = 2500000;
    std::vector<uint8_t> m_rbuf;
    struct ChunkState { uint32_t len = 0; uint8_t type = 0; uint32_t streamId = 0; bool extended = false; std::vector<uint8_t> payload; };
    std::map<uint32_t, ChunkState> m_in;
    bool     m_videoHeaderSent = false;
    bool     m_haveBase = false;
    uint32_t m_baseMs = 0;               // وقت أول قطعة بهالاتصال (منبلّش من صفر)
    uint64_t m_bytesSent = 0;
    bool     m_fatal = false;            // المنصة رفضت المفتاح: منطوّل قبل ما نعيد
};

// عناوين السيرفرات الجاهزة لكل منصة (فاضي = المستخدم لازم يحطّه)
std::wstring DefaultServer(Platform p);
std::wstring PlatformName(Platform p);
