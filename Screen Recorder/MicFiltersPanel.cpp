// =============================================================
//  MicFiltersPanel.cpp - نافذة فلاتر الميكروفون
// =============================================================
#include "MicFiltersPanel.h"
#include <algorithm>
#include <cmath>

namespace
{
    // أرقام عناصر النافذة
    enum : int
    {
        ID_NOISE_TOGGLE = ID_PANEL_USER, ID_NOISE_RESET, ID_NOISE_LEARN, ID_NOISE_STRENGTH,
        ID_GATE_TOGGLE, ID_GATE_RESET, ID_GATE_OPEN, ID_GATE_HOLD, ID_GATE_CLOSE, ID_GATE_METER,
        ID_LEVEL_TOGGLE, ID_LEVEL_RESET, ID_LEVEL_GAIN, ID_LEVEL_RATIO
    };

    // أطوال الأقسام (لازم تطابق الرسم)
    constexpr float GAP       = 12;
    constexpr float METERS_H  = 204;
    constexpr float NOISE_H   = 64 + 44 + 52;
    constexpr float GATE_H    = 64 + 44 * 3 + 30;
    constexpr float LEVEL_H   = 64 + 44 * 2 + 30;

    std::wstring Db(float v, bool plus = false)
    {
        const int n = (int)std::lround(v);
        return LtrText((plus && n > 0 ? L"+" : L"") + std::to_wstring(n)) + L" ديسيبل";
    }
    std::wstring Ms(float v)      { return std::to_wstring((int)std::lround(v)) + L" ملّي ثانية"; }
    std::wstring Percent(float v) { return LtrText(std::to_wstring((int)std::lround(v * 100)) + L"%"); }
    std::wstring Ratio(float v)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f : 1", v);
        return LtrText(buf);
    }

    // من ديسيبل (-60..0) لمكان على المقياس (0..1)
    float DbToMeter(float db) { return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f); }
}

MicFiltersPanel::MicFiltersPanel(AudioSource& mic, std::function<bool()> active, std::function<void()> save)
    : m_mic(mic), m_f(mic.Filters()), m_active(std::move(active)), m_save(std::move(save))
{
}

float MicFiltersPanel::BodyHeight() const
{
    return METERS_H + GAP + NOISE_H + GAP + GATE_H + GAP + LEVEL_H + 12;
}

// -------------------------------------------------------------
//  المقاييس: الخام، بعد إزالة الضجيج (مع حد البوابة)، والناتج
//  مع أرقام بالديسيبل لحتى تعرف بالضبط قديش الفلتر عم يشيل
// -------------------------------------------------------------
namespace
{
    // من مكان المقياس (0..1) لديسيبل
    float MeterToDb(float m) { return m * 60.0f - 60.0f; }
    std::wstring LevelText(float m)
    {
        return m <= 0.001f ? std::wstring(L"أقل من ") + LtrText(L"-60") : Db(MeterToDb(m));
    }
}

float MicFiltersPanel::DrawMeters(float y, float L, float R)
{
    const D2D1_RECT_F card = Rect(L, y, R, y + METERS_H);
    Fill(card, Theme::Card, 12);

    const bool active = m_active();
    // المستوى بيطلع فوراً وبينزل بنعومة
    m_rawLevel = std::max(PeakToMeter(m_f.TakeRawPeak()),    m_rawLevel * 0.88f);
    m_inLevel  = std::max(PeakToMeter(m_f.TakeInputPeak()),  m_inLevel * 0.88f);
    m_outLevel = std::max(PeakToMeter(m_f.TakeOutputPeak()), m_outLevel * 0.88f);
    if (!active)
        m_rawLevel = m_inLevel = m_outLevel = 0;
    // الأرقام منحدّثها 5 مرات بالثانية بس (لحتى تنقرا)
    if (++m_numberTick >= 6)
    {
        m_numberTick = 0;
        m_shownRaw = m_rawLevel; m_shownIn = m_inLevel; m_shownOut = m_outLevel;
    }

    const float l = L + 16, r = R - 16;
    Text(L"مستوى الصوت", g_fBody.Get(), Rect(l + 200, y + 10, r, y + 32), Theme::Text);

    // حالة البوابة على اليسار
    if (!active)
        Text(L"الميكروفون غير مفعّل", g_fSmallEnd.Get(), Rect(l, y + 10, l + 200, y + 32), Theme::Yellow);
    else if (m_f.gate)
    {
        const bool open = m_f.GateIsOpen();
        Circle(l + 6, y + 21, 4.5f, open ? Theme::Green : Theme::Off);
        Text(open ? L"البوابة مفتوحة" : L"البوابة مغلقة", g_fSmallEnd.Get(), Rect(l + 16, y + 10, l + 200, y + 32),
             open ? Theme::Green : Theme::SubText);
    }

    // سطر مقياس: الاسم يمين، الرقم يسار، والمقياس بالنص
    auto row = [&](float ry, const wchar_t* label, float level, float shown, float thick) -> D2D1_RECT_F
    {
        const float cy = ry + 14;
        Text(label, g_fSmall.Get(), Rect(r - 130, ry, r, ry + 28), Theme::SubText);
        Text(active ? LevelText(shown) : std::wstring(L"—"), g_fSmallEnd.Get(), Rect(l, ry, l + 100, ry + 28), Theme::Text);
        const D2D1_RECT_F m = Rect(l + 108, cy - thick / 2, r - 140, cy + thick / 2);
        const float mw = m.right - m.left;
        Fill(m, Theme::Border, thick / 2);
        if (level > 0)
        {
            const D2D1_COLOR_F c = level < 0.7f ? Theme::Green : (level < 0.9f ? Theme::Yellow : Theme::Red);
            Fill(Rect(m.right - mw * level, m.top, m.right, m.bottom), c, thick / 2);
        }
        return m;
    };

    // 1) الخام + الخط الأصفر (الضجيج اللي تعلّمه)
    const D2D1_RECT_F raw = row(y + 40, L"الميكروفون الخام", m_rawLevel, m_shownRaw, 8);
    if (m_f.noise && m_f.LearnedNoiseDb() > -150)
    {
        const float nx = raw.right - (raw.right - raw.left) * DbToMeter(m_f.LearnedNoiseDb());
        Line(nx, raw.top - 5, nx, raw.bottom + 5, Theme::Yellow, 2.0f);
    }

    // 2) بعد إزالة الضجيج (هاد اللي البوابة بتسمعه) + خط البوابة
    const D2D1_RECT_F meter = row(y + 76, L"بعد إزالة الضجيج", m_inLevel, m_shownIn, 14);
    if (m_f.gate)
    {
        const float mw = meter.right - meter.left;
        const float gx = meter.right - mw * DbToMeter(m_f.gateOpenDb);
        // اللي تحت الحد بيبيّن غامق (هاد الصوت اللي البوابة رح تسكّره)
        Fill(Rect(gx, meter.top, meter.right, meter.bottom), D2D1::ColorF(0x000000, 0.35f), 4);
        const D2D1_RECT_F hit = Rect(meter.left - 6, meter.top - 12, meter.right + 6, meter.bottom + 10);
        const bool hover = Inside(hit, g_mouse);
        Line(gx, meter.top - 6, gx, meter.bottom + 6, Theme::White, hover ? 3.0f : 2.0f);
        Circle(gx, meter.top - 7, hover ? 5.0f : 4.0f, Theme::White);
        // المقياس من -60 لـ 0، والحد من -60 لـ -10
        AddSlider(hit, meter, m_f.gateOpenDb, -60.0f, 0.0f, 1.0f,
                  ID_GATE_METER, [this](float v) { m_f.gateOpenDb = std::clamp(v, -60.0f, -10.0f); });
    }

    // 3) الناتج النهائي
    row(y + 112, L"الناتج النهائي", m_outLevel, m_shownOut, 8);

    // الخلاصة: قديش عم يشيل الفلتر هلق
    const float sy = y + 148;
    Fill(Rect(l, sy, r, sy + 1), Theme::Border);
    if (active && m_f.noise && m_shownRaw > 0)
    {
        const float rawDb = MeterToDb(m_shownRaw);
        std::wstring txt;
        if (m_shownIn <= 0.001f)
            txt = L"التخفيض الآن: أكثر من " + LtrText(std::to_wstring((int)std::lround(rawDb + 60))) + L" ديسيبل";
        else
            txt = L"التخفيض الآن: " + LtrText(std::to_wstring((int)std::lround(std::max(0.0f, rawDb - MeterToDb(m_shownIn))))) + L" ديسيبل";
        Text(txt, g_fBody.Get(), Rect(l + 150, sy + 8, r, sy + 30), Theme::Green);
        Text(L"(قِسه وأنت صامت)", g_fSmall.Get(), Rect(l + 150, sy + 30, r, sy + 48), Theme::SubText);
    }
    else
        Text(L"شغّل إزالة الضجيج لترى مقدار التخفيض", g_fSmall.Get(), Rect(l + 150, sy + 8, r, sy + 30), Theme::SubText);

    if (m_f.gate)
        Text(L"اسحب الخط الأبيض لتغيير حدّ البوابة", g_fSmallEnd.Get(), Rect(l, sy + 8, l + 230, sy + 30), Theme::Off);

    return y + METERS_H;
}

// -------------------------------------------------------------
//  الرسم
// -------------------------------------------------------------
void MicFiltersPanel::DrawBody(float L, float top, float R)
{
    float y = DrawMeters(top, L, R) + GAP;

    // ---- 1) إزالة الضجيج ----
    {
        const bool on = m_f.noise;
        const D2D1_RECT_F card = Rect(L, y, R, y + NOISE_H);
        float cy = Section(card, L"إزالة الضجيج", L"المروحة، المكيّف، الطنين", on, ID_NOISE_TOGGLE, ID_NOISE_RESET);
        const float l = L + 16, r = R - 16;
        cy = SliderRow(cy - 4, l, r, L"القوة", Percent(m_f.noiseStrength), m_f.noiseStrength, 0.0f, 2.0f, 0.05f,
                       ID_NOISE_STRENGTH, [this](float v) { m_f.noiseStrength = v; }, !on);

        // زر إعادة التعلّم
        DrawButton(Rect(r - 210, cy + 2, r, cy + 36), L"تعلّم الضجيج من جديد", ID_NOISE_LEARN, false, on, Icon::Refresh);
        const float learned = m_f.LearnedNoiseDb();
        if (m_learnFlash > 0)
            --m_learnFlash;
        if (on && (m_learnFlash > 0 || m_f.IsLearning()))
            Text(L"اصمت لحظة... جارٍ التعلّم", g_fSmallEnd.Get(), Rect(l, cy + 2, r - 220, cy + 36), Theme::Green);
        else if (on && learned > -150)
        {
            // النتيجة: النقطة الصفرا = نفس الخط الأصفر على المقياس
            Circle(l + 5, cy + 19, 4.0f, Theme::Yellow);
            Text(L"ضجيج الخلفية: " + Db(learned), g_fSmallEnd.Get(), Rect(l + 14, cy + 2, r - 220, cy + 36), Theme::Text);
        }
        else
            Text(L"اضغط ثم اصمت لحظة", g_fSmallEnd.Get(), Rect(l, cy + 2, r - 220, cy + 36), Theme::SubText);
        y += NOISE_H + GAP;
    }

    // ---- 2) البوابة ----
    {
        const bool on = m_f.gate;
        const D2D1_RECT_F card = Rect(L, y, R, y + GATE_H);
        float cy = Section(card, L"البوابة", L"تُسكت الميكروفون حين لا تتكلم", on, ID_GATE_TOGGLE, ID_GATE_RESET);
        const float l = L + 16, r = R - 16;
        cy = SliderRow(cy - 4, l, r, L"حدّ الفتح", Db(m_f.gateOpenDb), m_f.gateOpenDb, -60.0f, -10.0f, 1.0f,
                       ID_GATE_OPEN, [this](float v) { m_f.gateOpenDb = v; }, !on);
        cy = SliderRow(cy, l, r, L"مدة الإمساك", Ms(m_f.gateHoldMs), m_f.gateHoldMs, 0.0f, 1000.0f, 10.0f,
                       ID_GATE_HOLD, [this](float v) { m_f.gateHoldMs = v; }, !on);
        cy = SliderRow(cy, l, r, L"سرعة الإغلاق", Ms(m_f.gateCloseMs), m_f.gateCloseMs, 20.0f, 1000.0f, 10.0f,
                       ID_GATE_CLOSE, [this](float v) { m_f.gateCloseMs = v; }, !on);
        Text(L"إذا قُطع أول الكلام أو آخره: اخفض الحدّ أو زِد مدة الإمساك", g_fSmall.Get(),
             Rect(l, cy - 2, r, cy + 20), Theme::SubText);
        y += GATE_H + GAP;
    }

    // ---- 3) توحيد العلو ----
    {
        const bool on = m_f.leveler;
        const D2D1_RECT_F card = Rect(L, y, R, y + LEVEL_H);
        float cy = Section(card, L"توحيد مستوى الصوت", L"يرفع الصوت المنخفض ويخفض المرتفع", on, ID_LEVEL_TOGGLE, ID_LEVEL_RESET);
        const float l = L + 16, r = R - 16;
        cy = SliderRow(cy - 4, l, r, L"رفع الصوت", Db(m_f.levelGainDb, true), m_f.levelGainDb, 0.0f, 20.0f, 1.0f,
                       ID_LEVEL_GAIN, [this](float v) { m_f.levelGainDb = v; }, !on);
        cy = SliderRow(cy, l, r, L"قوة التوحيد", Ratio(m_f.levelRatio), m_f.levelRatio, 1.5f, 8.0f, 0.5f,
                       ID_LEVEL_RATIO, [this](float v) { m_f.levelRatio = v; }, !on);
        Text(L"كلما زادت القوة قلّ الفرق بين الصوت المنخفض والمرتفع", g_fSmall.Get(),
             Rect(l, cy - 2, r, cy + 20), Theme::SubText);
    }
}

// -------------------------------------------------------------
//  الكبسات
// -------------------------------------------------------------
void MicFiltersPanel::OnClick(int id)
{
    switch (id)
    {
    case ID_NOISE_TOGGLE: m_f.noise   = !m_f.noise;   break;
    case ID_GATE_TOGGLE:  m_f.gate    = !m_f.gate;    break;
    case ID_LEVEL_TOGGLE: m_f.leveler = !m_f.leveler; break;
    case ID_NOISE_RESET:  m_f.ResetNoise();   break;
    case ID_GATE_RESET:   m_f.ResetGate();    break;
    case ID_LEVEL_RESET:  m_f.ResetLeveler(); break;
    case ID_NOISE_LEARN:
        if (!m_f.noise)
            return;
        m_f.RelearnNoise();
        m_learnFlash = 10;      // لحد ما يبلّش التعلّم فعلياً
        return;
    default:
        return;
    }
    m_save();
}
