// =============================================================
//  AudioFilters.h - فلاتر المايك
//
//   1) إزالة الضجيج: بيتعلّم صوت الخلفية الثابت (مروحة، مكيّف، طنين)
//      وبيشيله من الصوت (طريقة "الطرح الطيفي").
//   2) البوابة: لما ما تحكي بتسكّر المايك (ما بيطلع صوت كيبورد ونفس).
//   3) توحيد العلو: الصوت الواطي بيعلى والعالي بينزل (ضاغط + محدّد).
//
//  كلها بتشتغل على 48000 هرتز ستيريو.
// =============================================================
#pragma once
#include <atomic>
#include <complex>
#include <cstddef>
#include <deque>
#include <vector>

class MicFilters
{
public:
    MicFilters();

    std::atomic<bool> noise{ false };     // إزالة الضجيج
    std::atomic<bool> gate{ false };      // البوابة
    std::atomic<bool> leveler{ false };   // توحيد العلو

    bool Any() const { return noise || gate || leveler; }

    // ---------- الإعدادات (بتتغيّر من نافذة الفلاتر وبتشتغل فوراً) ----------
    // القيم الافتراضية
    static constexpr float DEF_NOISE_STRENGTH = 0.5f;    // 0..2 (يعني 0%..200%)
    static constexpr float DEF_GATE_OPEN_DB   = -42;     // -60..-10
    static constexpr float DEF_GATE_HOLD_MS   = 250;     // 0..1000
    static constexpr float DEF_GATE_CLOSE_MS  = 120;     // 20..1000
    static constexpr float DEF_LEVEL_GAIN_DB  = 10;      // 0..20
    static constexpr float DEF_LEVEL_RATIO    = 3.5f;    // 1.5..8

    std::atomic<float> noiseStrength{ DEF_NOISE_STRENGTH };  // قوة إزالة الضجيج
    std::atomic<float> gateOpenDb{ DEF_GATE_OPEN_DB };       // حد فتح البوابة
    std::atomic<float> gateHoldMs{ DEF_GATE_HOLD_MS };       // قديش بتضل مفتوحة بعد ما تسكت
    std::atomic<float> gateCloseMs{ DEF_GATE_CLOSE_MS };     // سرعة الإغلاق
    std::atomic<float> levelGainDb{ DEF_LEVEL_GAIN_DB };     // رفع الصوت
    std::atomic<float> levelRatio{ DEF_LEVEL_RATIO };        // قوة التوحيد

    void ResetNoise()   { noiseStrength = DEF_NOISE_STRENGTH; }
    void ResetGate()    { gateOpenDb = DEF_GATE_OPEN_DB; gateHoldMs = DEF_GATE_HOLD_MS; gateCloseMs = DEF_GATE_CLOSE_MS; }
    void ResetLeveler() { levelGainDb = DEF_LEVEL_GAIN_DB; levelRatio = DEF_LEVEL_RATIO; }

    // إزالة الضجيج بتعيد تتعلّم صوت الخلفية (اسكت لحظة بعدها)
    void RelearnNoise() { m_relearn = true; }
    // مستوى ضجيج الخلفية اللي تعلّمه (ديسيبل). أوطى من -150 = لسا ما تعلّم
    float LearnedNoiseDb() const { return m_learnedDb; }
    bool  IsLearning() const     { return m_learningNow; }

    // ---------- للمقاييس بنافذة الفلاتر ----------
    float TakeRawPeak()    { return m_rawPeak.exchange(0.0f); }   // الميكروفون الخام (قبل أي فلتر)
    float TakeInputPeak()  { return m_inPeak.exchange(0.0f); }    // قبل البوابة (الصوت اللي البوابة بتسمعه)
    float TakeOutputPeak() { return m_outPeak.exchange(0.0f); }   // بعد كل الفلاتر
    bool  GateIsOpen() const { return m_gateOpen; }

    // بيعالج الصوت بمكانه (يسار، يمين، يسار، يمين...)
    void Process(float* stereo, size_t frames);

private:
    // إزالة الضجيج
    static constexpr int N = 512, HOP = 256;
    void   Denoise(float* mono, size_t count);
    void   SpectralFrame();
    std::vector<float> m_window, m_frame, m_ola, m_pending;
    std::deque<float>  m_out;
    std::vector<std::complex<float>> m_spec;
    std::vector<float> m_noise, m_smooth, m_gain;
    int                m_frames = 0;
    bool               m_wasOn = false;

    // البوابة
    float m_gateEnv = 0, m_gateGain = 0;
    int   m_gateHold = 0;
    std::atomic<bool> m_gateOpen{ false };

    // المقاييس
    std::atomic<float> m_rawPeak{ 0 }, m_inPeak{ 0 }, m_outPeak{ 0 };
    std::atomic<bool>  m_relearn{ false };
    std::atomic<float> m_learnedDb{ -200 };
    std::atomic<bool>  m_learningNow{ false };
    float m_learnSum = 0;
    int   m_learnCount = 0;
    static constexpr int LEARN_FRAMES = 94;       // 94 × 256 عينة ≈ 0.5 ثانية
    static void KeepMax(std::atomic<float>& a, float v) { if (v > a.load()) a = v; }

    // توحيد العلو
    float m_compEnv = 0;

    std::vector<float> m_mono;
};
