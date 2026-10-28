// =============================================================
//  Audio.h - مصدر صوت: صوت الكمبيوتر أو المايك
//
//  كل مصدر بيشتغل بخيط لحالو وبياخد الصوت من ويندوز (WASAPI)
//  وبيحوّله لشكل موحّد: 48000 عينة بالثانية، ستيريو (قناتين)
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX      // بيمنع ويندوز يخرّب std::min و std::max
#endif

#include <windows.h>
#include <atomic>
#include <deque>
#include <map>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "AudioFilters.h"

// جهاز صوت (سماعة أو ميكروفون)
struct AudioDevice
{
    std::wstring id;     // رقمه عند ويندوز
    std::wstring name;   // اسمه
};

class AudioSource
{
public:
    enum class Kind { System, Microphone };

    static constexpr UINT32 SAMPLE_RATE = 48000;
    static constexpr UINT32 CHANNELS    = 2;

    explicit AudioSource(Kind kind) : m_kind(kind) {}
    ~AudioSource() { Stop(); }

    bool Start();                 // بلّش ياخد صوت (بيرجّع false إذا ما في جهاز)
    void Stop();
    bool IsRunning() const { return m_running; }

    // أعلى مستوى صوت من آخر مرة سألنا (0 = سكوت ، 1 = أعلى شي)
    float TakePeak() { return m_peak.exchange(0.0f); }

    std::wstring DeviceName();

    // علو الصوت (0..2 ، 1 = طبيعي) والكتم - بيشتغلوا حتى وقت التسجيل
    void  SetVolume(float v) { m_volume = v < 0 ? 0 : (v > 2 ? 2 : v); }
    float Volume() const     { return m_volume; }
    void  SetMuted(bool m)   { m_muted = m; }
    bool  IsMuted() const    { return m_muted; }

    // فلاتر الصوت (للمايك): إزالة الضجيج ، البوابة ، توحيد العلو
    MicFilters& Filters() { return m_filters; }

    // اختيار الجهاز: id فاضي = الجهاز الافتراضي بويندوز
    void         SetDeviceId(const std::wstring& id) { m_deviceId = id; }
    std::wstring DeviceId() const { return m_deviceId; }

    // كل الأجهزة الشغّالة من هالنوع
    static std::vector<AudioDevice> ListDevices(Kind kind);

    // كل مستهلك (التسجيل، البث) بياخد "حنفية" إلها مخزن خاص
    int    AddTap();
    void   RemoveTap(int tap);
    // "مسلّح" = عم يخزّن الصوت. clear = امسح المخزّن القديم
    void   SetArmed(int tap, bool armed, bool clear);
    // خود عدد frames من الصوت المخزّن (كل frame = يسار + يمين)
    size_t Read(int tap, float* dst, size_t frames);
    void   KeepNewest(int tap, size_t frames);    // ارمي القديم إذا تجمّع كتير

private:
    void ThreadMain(std::promise<bool>* ready);   // ready: منقول فيه إذا الجهاز اشتغل
    void Process(const BYTE* data, UINT32 frames, bool silent);
    float SampleAt(const BYTE* data, UINT32 frame, UINT32 channel) const;

    Kind               m_kind;
    std::atomic<bool>  m_running{ false };
    std::atomic<float> m_peak{ 0.0f };
    std::atomic<float> m_volume{ 1.0f };
    std::atomic<bool>  m_muted{ false };
    std::thread        m_thread;
    MicFilters         m_filters;

    struct Tap
    {
        std::deque<float> fifo;          // الصوت المخزّن
        bool              armed = false;
    };
    std::mutex         m_mutex;          // بيحمي m_taps و m_name
    std::map<int, Tap> m_taps;
    int                m_nextTap = 1;
    std::wstring       m_name;
    std::wstring       m_deviceId;       // فاضي = الافتراضي

    // شكل الصوت اللي جاي من الجهاز
    UINT32 m_srcChannels = 2, m_srcRate = 48000, m_srcBits = 32, m_srcBlock = 8;
    bool   m_srcFloat = true;

    // لتحويل سرعة العينات (مثلاً 44100 -> 48000)
    double m_step = 1.0, m_phase = 0.0;
    float  m_prevL = 0.0f, m_prevR = 0.0f;
};
