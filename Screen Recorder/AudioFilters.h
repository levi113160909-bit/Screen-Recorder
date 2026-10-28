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

    // توحيد العلو
    float m_compEnv = 0;

    std::vector<float> m_mono;
};
