// =============================================================
//  AudioFilters.cpp
// =============================================================
#include "AudioFilters.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float PI = 3.14159265358979f;
    constexpr float RATE = 48000.0f;

    // تحويل فورييه السريع (بيفصل الصوت لترددات)
    void Fft(std::vector<std::complex<float>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const float ang = 2 * PI / (float)len * (inverse ? 1.0f : -1.0f);
            const std::complex<float> wlen(std::cos(ang), std::sin(ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w(1, 0);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j]           = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
        if (inverse)
            for (auto& x : a) x /= (float)n;
    }

    float Coef(float ms) { return std::exp(-1.0f / (RATE * ms / 1000.0f)); }
    float DbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
    float LinToDb(float v) { return 20.0f * std::log10(std::max(v, 1e-9f)); }
}

MicFilters::MicFilters()
{
    // نافذة "جذر هان": بالتداخل 50% مجموعها بيرجع الصوت متل ما هو
    m_window.resize(N);
    for (int i = 0; i < N; ++i)
        m_window[i] = std::sqrt(0.5f - 0.5f * std::cos(2 * PI * i / N));
    m_frame.assign(N, 0);
    m_ola.assign(N, 0);
    m_spec.resize(N);
    m_noise.assign(N / 2 + 1, 0);
    m_smooth.assign(N / 2 + 1, 0);
    m_gain.assign(N / 2 + 1, 1);
}

void MicFilters::Process(float* stereo, size_t frames)
{
    if (!Any())
    {
        // ولا فلتر شغّال: بس منقيس المستوى لمقاييس نافذة الفلاتر
        float peak = 0;
        for (size_t i = 0; i < frames * 2; ++i)
            peak = std::max(peak, std::fabs(stereo[i]));
        KeepMax(m_rawPeak, peak);
        KeepMax(m_inPeak, peak);
        KeepMax(m_outPeak, peak);
        m_gateOpen = true;
        m_wasOn = false;
        return;
    }

    // المايك عادة مونو: منشتغل على قناة وحدة ومنرجّعها للتنتين
    m_mono.resize(frames);
    float rawPeak = 0;
    for (size_t i = 0; i < frames; ++i)
    {
        m_mono[i] = (stereo[i * 2] + stereo[i * 2 + 1]) * 0.5f;
        rawPeak = std::max(rawPeak, std::fabs(m_mono[i]));
    }
    KeepMax(m_rawPeak, rawPeak);

    // 1) إزالة الضجيج
    if (noise)
        Denoise(m_mono.data(), frames);
    else if (!m_out.empty())
    {
        m_out.clear();
        m_pending.clear();
    }

    // المستوى اللي البوابة رح تسمعه (بعد إزالة الضجيج)
    {
        float peak = 0;
        for (size_t i = 0; i < frames; ++i)
            peak = std::max(peak, std::fabs(m_mono[i]));
        KeepMax(m_inPeak, peak);
    }

    // 2) البوابة
    if (gate)
    {
        const float openDb  = std::clamp(gateOpenDb.load(), -60.0f, -10.0f);
        const float openTh  = DbToLin(openDb), closeTh = DbToLin(openDb - 8);   // بتسكّر أوطى بشوي (ما بترجف)
        const float envAtt  = Coef(1), envRel = Coef(60);
        const float gAtt    = 1 - Coef(4), gRel = 1 - Coef(std::clamp(gateCloseMs.load(), 20.0f, 1000.0f));
        const int   holdSamples = (int)(RATE * std::clamp(gateHoldMs.load(), 0.0f, 1000.0f) / 1000.0f);
        for (size_t i = 0; i < frames; ++i)
        {
            const float a = std::fabs(m_mono[i]);
            m_gateEnv = a > m_gateEnv ? envAtt * m_gateEnv + (1 - envAtt) * a : envRel * m_gateEnv + (1 - envRel) * a;
            float target = 0;
            if (m_gateEnv > openTh)            { m_gateHold = holdSamples; target = 1; }
            else if (m_gateHold > 0)           { --m_gateHold; target = 1; }
            else if (m_gateEnv > closeTh && m_gateGain > 0.5f) target = 1;
            m_gateGain += (target - m_gateGain) * (target > m_gateGain ? gAtt : gRel);
            m_mono[i] *= m_gateGain;
        }
        m_gateOpen = m_gateGain > 0.5f;
    }
    else
        m_gateOpen = true;

    // 3) توحيد العلو: ضاغط (بيقرّب الواطي من العالي) + رفع + محدّد (ما بيخلّي الصوت يتشوّه)
    if (leveler)
    {
        const float thresholdDb = -24;
        const float ratio    = std::clamp(levelRatio.load(), 1.5f, 8.0f);
        const float makeupDb = std::clamp(levelGainDb.load(), 0.0f, 20.0f);
        const float att = Coef(5), rel = Coef(200);
        for (size_t i = 0; i < frames; ++i)
        {
            const float a = std::fabs(m_mono[i]);
            m_compEnv = a > m_compEnv ? att * m_compEnv + (1 - att) * a : rel * m_compEnv + (1 - rel) * a;
            const float envDb = LinToDb(m_compEnv);
            float gainDb = makeupDb;
            if (envDb > thresholdDb)
                gainDb -= (envDb - thresholdDb) * (1 - 1 / ratio);
            float y = m_mono[i] * DbToLin(gainDb);
            // محدّد ناعم عند -1 ديسيبل
            const float lim = 0.89f;
            if (y > lim)       y = lim + (1 - lim) * std::tanh((y - lim) / (1 - lim));
            else if (y < -lim) y = -lim - (1 - lim) * std::tanh((-y - lim) / (1 - lim));
            m_mono[i] = y;
        }
    }

    float outPeak = 0;
    for (size_t i = 0; i < frames; ++i)
    {
        stereo[i * 2] = stereo[i * 2 + 1] = m_mono[i];
        outPeak = std::max(outPeak, std::fabs(m_mono[i]));
    }
    KeepMax(m_outPeak, outPeak);
    m_wasOn = true;
}

// -------------------------------------------------------------
// إزالة الضجيج: كل 256 عينة منحلّل آخر 512 لترددات،
// ومنقارن كل تردد بمستوى الضجيج اللي تعلّمناه
// -------------------------------------------------------------
void MicFilters::Denoise(float* mono, size_t count)
{
    if (m_out.empty())
    {
        m_out.assign(HOP, 0.0f);         // تأخير صغير (حوالي 10 ملي ثانية)
        m_pending.clear();
        m_frame.assign(N, 0);
        m_ola.assign(N, 0);
    }
    for (size_t i = 0; i < count; ++i)
    {
        m_pending.push_back(mono[i]);
        if ((int)m_pending.size() == HOP)
        {
            // منزحلق الإطار ومنحط الجديد بآخره
            std::copy(m_frame.begin() + HOP, m_frame.end(), m_frame.begin());
            std::copy(m_pending.begin(), m_pending.end(), m_frame.begin() + (N - HOP));
            m_pending.clear();
            SpectralFrame();
        }
        mono[i] = m_out.front();
        m_out.pop_front();
    }
}

void MicFilters::SpectralFrame()
{
    for (int i = 0; i < N; ++i)
        m_spec[i] = std::complex<float>(m_frame[i] * m_window[i], 0);
    Fft(m_spec, false);

    const int bins = N / 2 + 1;
    if (m_relearn.exchange(false))
        m_frames = 0;                             // منبلّش نتعلّم الضجيج من جديد
    ++m_frames;

    // القوة: 0 = خفيفة ، 1 = قوية ، 2 = قوية جداً (200%)
    const float strength = std::clamp(noiseStrength.load(), 0.0f, 2.0f);
    const float overSub  = 1.0f + 2.0f * strength;                 // قديش منطرح (1..5)
    const float floorG   = DbToLin(-10.0f - 24.0f * strength);     // أقصى تخفيض (-10..-58 ديسيبل)
    // أول نص ثانية: منتعلّم الضجيج (لازم تكون ساكت)
    const bool learning = m_frames <= LEARN_FRAMES;
    m_learningNow = learning;
    if (learning)
    {
        // منقيس قوة الضجيج (قبل الفلتر) لحتى نبيّنها بنافذة الفلاتر
        if (m_frames == 1) { m_learnSum = 0; m_learnCount = 0; }
        float peak = 0;
        for (int i = N - HOP; i < N; ++i)
            peak = std::max(peak, std::fabs(m_frame[i]));
        m_learnSum += peak;
        ++m_learnCount;
        if (m_frames == LEARN_FRAMES)
            m_learnedDb = LinToDb(m_learnSum / m_learnCount);
    }

    for (int k = 0; k < bins; ++k)
    {
        const float power = std::norm(m_spec[k]);
        m_smooth[k] = 0.75f * m_smooth[k] + 0.25f * power;

        // مستوى الضجيج = "متوسط" صوت الخلفية (مش أوطى شي).
        // قبل كنا ناخد الأوطى، وهاد بيطلع أوطى من الضجيج الحقيقي بكتير،
        // فالفلتر كان يفكّر إن الضجيج صوتك وما يشيله.
        if (learning)
            m_noise[k] += (m_smooth[k] - m_noise[k]) / (float)m_frames;     // متوسط حقيقي
        else if (m_smooth[k] < m_noise[k] * 3.0f)
            m_noise[k] = 0.97f * m_noise[k] + 0.03f * m_smooth[k];         // غالباً ضجيج: منلحقه
        else
            m_noise[k] *= 1.0003f;                // صوتك: بالكاد منتحرّك (0.25 ديسيبل بالثانية)

        // كم منخلّي من هالتردد
        const float snr = m_smooth[k] / std::max(m_noise[k], 1e-12f);
        float g = 1.0f - overSub / std::max(snr, 1e-6f);     // طرح (أقوى = منطرح أكتر)
        g = std::clamp(g, floorG, 1.0f);                       // أقصى تخفيض حسب القوة
        // تنعيم مع الوقت (بيمنع صوت "الفقاعات")
        m_gain[k] = g > m_gain[k] ? 0.6f * g + 0.4f * m_gain[k] : 0.35f * g + 0.65f * m_gain[k];
    }

    // تنعيم بين الترددات الجارة
    for (int k = 0; k < bins; ++k)
    {
        const float a = m_gain[std::max(0, k - 1)], b = m_gain[k], c = m_gain[std::min(bins - 1, k + 1)];
        const float g = 0.25f * a + 0.5f * b + 0.25f * c;
        m_spec[k] *= g;
        if (k > 0 && k < N / 2)
            m_spec[N - k] *= g;
    }

    Fft(m_spec, true);
    for (int i = 0; i < N; ++i)
        m_ola[i] += m_spec[i].real() * m_window[i];
    for (int i = 0; i < HOP; ++i)
        m_out.push_back(m_ola[i]);
    std::copy(m_ola.begin() + HOP, m_ola.end(), m_ola.begin());
    std::fill(m_ola.end() - HOP, m_ola.end(), 0.0f);
}
