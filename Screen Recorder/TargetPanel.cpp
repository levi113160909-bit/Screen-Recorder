// =============================================================
//  TargetPanel.cpp - نافذة "إعدادات منصة البث"
// =============================================================
#include "TargetPanel.h"
#include <algorithm>

namespace
{
    enum : int
    {
        ID_CODEC   = ID_PANEL_USER,        // +0..1
        ID_MODE    = ID_PANEL_USER + 10,   // +0..1 (عام / مخصّص)
        ID_PRESET  = ID_PANEL_USER + 20,   // +0..4
        ID_MINUS   = ID_PANEL_USER + 30,
        ID_PLUS
    };
    const UINT32 PRESETS[] = { 6000, 12000, 17000, 24000, 34000 };
    constexpr UINT32 KBPS_MIN = 1000, KBPS_MAX = 50000;

    std::wstring Num(UINT32 v) { return LtrText(std::to_wstring(v)); }
}

UINT32 YouTubeRecommendedKbps(UINT32 w, UINT32 h, UINT32 fps, bool hevc)
{
    // جدول يوتيوب (ميغابت/ث): HEVC/AV1 ثم H.264
    const UINT32 s = std::min(w, h);
    const bool hi = fps > 30;
    if (s >= 2000) return hevc ? (hi ? 35000 : 30000) : (hi ? 50000 : 42000);   // 2160p
    if (s >= 1300) return hevc ? (hi ? 24000 : 15000) : (hi ? 34000 : 21000);   // 1440p
    if (s >= 1000) return hevc ? (hi ? 12000 : 10000) : (hi ? 17000 : 14000);   // 1080p
    if (s >= 700)  return hevc ? 6000 : 8000;                                    // 720p
    return hevc ? 3000 : 4000;
}

TargetPanel::TargetPanel(TargetPanelHooks hooks) : m_h(std::move(hooks)) {}

std::wstring TargetPanel::Title() const
{
    const StreamTarget* t = m_h.target();
    return t ? L"إعدادات " + t->name : L"إعدادات المنصة";
}

void TargetPanel::DrawBody(float L, float top, float R)
{
    StreamTarget* t = m_h.target();
    if (!t)
        return;
    float y = top;
    auto Card = [&](float h) { Fill(Rect(L, y, R, y + h), Theme::Card, 12); };
    auto Head = [&](const std::wstring& s) { Text(s, g_fSection.Get(), Rect(L + 16, y + 10, R - 16, y + 32), Theme::Text); };
    auto Hint = [&](const std::wstring& s, float yy, D2D1_COLOR_F c = Theme::SubText)
    {
        Text(s, g_fSmall.Get(), Rect(L + 16, yy, R - 16, yy + 18), c);
    };
    const float l = L + 16, r = R - 16;

    UINT32 w = 1920, h = 1080, fps = 60;
    m_h.format(w, h, fps);
    const bool hevcOk = PlatformSupportsHevc(t->platform);
    const bool hevc   = t->codec == 1 && hevcOk;

    // ---- 1) الترميز ----
    {
        Card(hevcOk ? 136.0f : 118.0f);
        Head(L"الترميز");
        DrawSegmented(Rect(l, y + 40, r, y + 74), { L"H.264", hevcOk ? L"HEVC (H.265)" : L"HEVC (غير مدعوم)" },
                      hevc ? 1 : 0, ID_CODEC, true);
        if (!hevcOk)
            Hint(L"هذه المنصة تستقبل H.264 فقط", y + 82);
        else if (hevc)
        {
            Hint(L"نفس الجودة بمعدّل بت أقل بنحو الثلث • يُرسَل عبر Enhanced RTMP", y + 82);
            Hint(t->platform == Platform::Custom ? L"تأكد أن الخادم يدعم Enhanced RTMP • يحتاج بطاقة NVIDIA (وإلا يُستخدم H.264)"
                                                 : L"يحتاج بطاقة NVIDIA (وإلا يُستخدم H.264 تلقائيًا)",
                 y + 104, Theme::Yellow);
        }
        else
            Hint(L"الأكثر توافقًا: يعمل مع كل المنصات والمشاهدين", y + 82);
        y += (hevcOk ? 136.0f : 118.0f) + 10;
    }

    // ---- 2) معدّل البت ----
    {
        const bool custom = t->kbps != 0;
        const UINT32 kbps = custom ? t->kbps : m_h.defaultKbps();
        const float ch = custom ? 222.0f : 140.0f;
        Card(ch);
        Head(L"معدّل البت");
        DrawSegmented(Rect(l, y + 40, r, y + 74),
                      { L"الإعداد العام (" + Num(m_h.defaultKbps()) + L")", L"مخصّص لهذه المنصة" },
                      custom ? 1 : 0, ID_MODE, true);
        float yy = y + 82;
        if (custom)
        {
            int preset = -1;
            for (int i = 0; i < 5; ++i)
                if (PRESETS[i] == t->kbps) preset = i;
            DrawSegmented(Rect(l, yy, r, yy + 34), { L"6000", L"12000", L"17000", L"24000", L"34000" }, preset, ID_PRESET, true);
            yy += 42;
            const D2D1_RECT_F box   = Rect(l, yy, r, yy + 34);
            const D2D1_RECT_F plus  = Rect(l + 3, yy + 3, l + 40, yy + 31);     // + على اليسار
            const D2D1_RECT_F minus = Rect(r - 40, yy + 3, r - 3, yy + 31);
            Fill(box, Theme::Background, 9);
            if (AddHit(plus, ID_PLUS))   Fill(plus, Theme::Hover, 7);
            if (AddHit(minus, ID_MINUS)) Fill(minus, Theme::Hover, 7);
            Text(L"+", g_fCenter.Get(), plus, Theme::Text);
            Text(L"−", g_fCenter.Get(), minus, Theme::Text);
            Text(Num(t->kbps) + L" كيلوبت/ث", g_fCenter.Get(), Rect(l + 44, yy, r - 44, yy + 34), Theme::Text);
            yy += 42;
        }
        // النصيحة
        const std::wstring fmt = Num(std::min(w, h)) + L"p" + Num(fps);
        if (t->platform == Platform::Twitch)
            Hint(L"تويتش ينصح بـ 6000 كحد أقصى لمعظم الحسابات", yy);
        else
            Hint(L"يوتيوب ينصح لـ " + fmt + L" بـ " + Num(YouTubeRecommendedKbps(w, h, fps, hevc)) +
                 (hevc ? L" (HEVC)" : L" (H.264)") + L" كيلوبت/ث", yy);
        const UINT32 rec = YouTubeRecommendedKbps(w, h, fps, hevc);
        if (t->platform != Platform::Twitch && kbps < rec * 6 / 10)
            Hint(L"المعدّل الحالي (" + Num(kbps) + L") أقل بكثير من المقترح: قد تظهر الحركة السريعة مشوّشة", yy + 22, Theme::Yellow);
        else
            Hint(L"سرعة الرفع لهذه المنصة ≈ " + Num((kbps + 160) * 12 / 10 / 1000 + 1) + L" ميغابت/ث", yy + 22);
        y += ch + 10;
    }

    // ---- 3) مشاركة الضغط ----
    {
        Card(70);
        Head(L"مشاركة الضغط");
        const std::wstring others = m_h.sharedWith();
        Hint(others.empty() ? std::wstring(L"تُضغط نسخة خاصة بهذه المنصة")
                            : L"تتشارك نفس الفيديو المضغوط مع: " + others + L" (بدون ضغط إضافي)", y + 40);
        y += 80;
    }
    m_height = y - top + 4;
}

void TargetPanel::OnClick(int id)
{
    StreamTarget* t = m_h.target();
    if (!t)
        return;
    if (id >= ID_CODEC && id < ID_CODEC + 2)
    {
        if (id == ID_CODEC + 1 && !PlatformSupportsHevc(t->platform))
            return;
        t->codec = id - ID_CODEC;
    }
    else if (id >= ID_MODE && id < ID_MODE + 2)
        t->kbps = (id == ID_MODE) ? 0 : (t->kbps ? t->kbps : m_h.defaultKbps());
    else if (id >= ID_PRESET && id < ID_PRESET + 5)
        t->kbps = PRESETS[id - ID_PRESET];
    else if (id == ID_MINUS || id == ID_PLUS)
    {
        const UINT32 cur = t->kbps ? t->kbps : m_h.defaultKbps();
        const UINT32 step = (id == ID_PLUS ? cur >= 10000 : cur > 10000) ? 1000 : 500;
        t->kbps = id == ID_PLUS ? std::min(KBPS_MAX, cur + step) : std::max(KBPS_MIN, cur - step);
    }
    else
        return;
    m_h.changed();
}
