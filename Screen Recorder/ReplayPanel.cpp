// =============================================================
//  ReplayPanel.cpp - نافذة "إعدادات الإعادة"
// =============================================================
#include "ReplayPanel.h"
#include <algorithm>
#include <cmath>

namespace
{
    enum : int
    {
        ID_SOURCE  = ID_PANEL_USER,        // +0..1
        ID_MONITOR = ID_PANEL_USER + 10,   // +0..3
        ID_DUR     = ID_PANEL_USER + 20,   // +0..3
        ID_RES     = ID_PANEL_USER + 30,   // +0..2
        ID_FPS     = ID_PANEL_USER + 40,   // +0..1
        ID_QUALITY = ID_PANEL_USER + 50,   // +0..2
        ID_SYSTEM  = ID_PANEL_USER + 60,
        ID_MIC,
        ID_AUTO,
        ID_FOLDER_CHANGE = ID_PANEL_USER + 70,
        ID_FOLDER_OPEN
    };
    const int SECONDS[] = { 15, 30, 60, 120 };
}

UINT32 ReplayKbps(const ReplaySettings& s, int w, int h)
{
    // الأساس: 1080p بـ 60 إطار ، وبيتغيّر حسب عدد البكسلات والإطارات
    const double base[] = { 8000, 14000, 22000 };
    const double fps = s.fps == 0 ? 30 : 60;
    const double k = base[std::clamp(s.quality, 0, 2)] * ((double)w * h * fps) / (1920.0 * 1080.0 * 60.0);
    return (UINT32)std::clamp(k, 2500.0, 50000.0);
}

ReplayPanel::ReplayPanel(ReplaySettings& settings, ReplayPanelHooks hooks)
    : m_s(settings), m_h(std::move(hooks))
{
}

void ReplayPanel::DrawBody(float L, float top, float R)
{
    float y = top;
    auto Card = [&](float h) { Fill(Rect(L, y, R, y + h), Theme::Card, 12); };
    auto Title = [&](const std::wstring& t) { Text(t, g_fSection.Get(), Rect(L + 16, y + 10, R - 16, y + 32), Theme::Text); };
    auto Hint = [&](const std::wstring& t, float yy, D2D1_COLOR_F c = Theme::SubText)
    {
        Text(t, g_fSmall.Get(), Rect(L + 16, yy, R - 16, yy + 18), c);
    };
    const float l = L + 16, r = R - 16;

    // ---- 1) شو بيتسجّل ----
    {
        const std::vector<std::wstring> mons = m_h.monitors();
        const bool pickMon = m_s.source == 0 && mons.size() > 1;
        const float h = pickMon ? 150.0f : 110.0f;
        Card(h);
        Title(L"ما الذي يُسجَّل");
        DrawSegmented(Rect(l, y + 40, r, y + 74), { L"الشاشة كاملة", L"المشهد الحالي" }, m_s.source, ID_SOURCE, true);
        float yy = y + 78;
        if (pickMon)
        {
            std::vector<std::wstring> names(mons.begin(), mons.begin() + std::min<size_t>(mons.size(), 4));
            DrawSegmented(Rect(l, yy, r, yy + 34), names, std::clamp(m_s.monitor, 0, (int)names.size() - 1), ID_MONITOR, true);
            yy += 40;
        }
        Hint(m_s.source == 0 ? L"يلتقط الشاشة كلها دائمًا، مهما كان المشهد أو فارغًا"
                             : L"يلتقط ما يظهر في المعاينة (مع أصوات برامج المشهد)", yy + 4);
        y += h + 10;
    }

    // ---- 2) الصوت ----
    {
        Card(120);
        Title(L"الصوت");
        auto ToggleRow = [&](float yy, const std::wstring& name, const std::wstring& sub, bool on, int id, bool enabled)
        {
            AddHit(Rect(l, yy, r, yy + 36), id);
            DrawToggle(l, yy + 6, on, enabled);
            Text(name, g_fBody.Get(), Rect(l + 60, yy, r, yy + 20), enabled ? Theme::Text : Theme::SubText);
            Text(sub, g_fSmall.Get(), Rect(l + 60, yy + 19, r, yy + 36), Theme::SubText);
        };
        ToggleRow(y + 40, L"صوت النظام", L"كل أصوات الكمبيوتر (اللعبة، المتصفح، ديسكورد...)", m_s.system, ID_SYSTEM, true);
        const bool micOk = m_h.micEnabled();
        ToggleRow(y + 80, L"الميكروفون",
                  micOk ? std::wstring(L"صوتك") : std::wstring(L"الميكروفون مُعطّل في تبويب المصادر"),
                  m_s.mic, ID_MIC, micOk);
        y += 130;
    }

    // ---- 3) المدة والجودة ----
    {
        Card(272);
        Title(L"المدة والجودة");
        Text(L"المدة المحفوظة", g_fSmall.Get(), Rect(l, y + 38, r, y + 56), Theme::SubText);
        DrawSegmented(Rect(l, y + 58, r, y + 92), { L"15 ث", L"30 ث", L"60 ث", L"دقيقتان" }, m_s.seconds, ID_DUR, true);
        Text(L"الدقة", g_fSmall.Get(), Rect(l, y + 100, r, y + 118), Theme::SubText);
        DrawSegmented(Rect(l, y + 120, r, y + 154), { L"الأصلية", L"1080p", L"720p" }, m_s.res, ID_RES, true);
        const float half = (r - l - 10) / 2;
        Text(L"الإطارات", g_fSmall.Get(), Rect(r - half, y + 162, r, y + 180), Theme::SubText);
        Text(L"الجودة", g_fSmall.Get(), Rect(l, y + 162, l + half, y + 180), Theme::SubText);
        DrawSegmented(Rect(r - half, y + 182, r, y + 216), { L"30", L"60" }, m_s.fps, ID_FPS, true);
        DrawSegmented(Rect(l, y + 182, l + half, y + 216), { L"منخفضة", L"متوسطة", L"عالية" }, m_s.quality, ID_QUALITY, true);

        // تقدير الحجم بالذاكرة
        int sw = std::max(2, m_h.sourceW()), sh = std::max(2, m_h.sourceH());
        int hh = m_s.res == 1 ? 1080 : (m_s.res == 2 ? 720 : sh);
        hh = std::min(hh, sh);
        const int ww = sw * hh / sh;
        const UINT32 kbps = ReplayKbps(m_s, ww, hh);
        const int mb = (int)std::lround(kbps / 8.0 * SECONDS[m_s.seconds] / 1000.0);
        Hint(L"يستهلك تقريبًا " + LtrText(std::to_wstring(mb)) + L" ميغابايت من الذاكرة • " +
             LtrText(std::to_wstring(kbps / 1000)) + L" ميغابت/ث", y + 228);
        y += 282;
    }

    // ---- 4) مكان الحفظ ----
    {
        Card(118);
        Title(L"مكان الحفظ");
        const D2D1_RECT_F box = Rect(l, y + 40, r, y + 74);
        Fill(box, Theme::Background, 8);
        Stroke(box, Theme::Border, 8);
        Text(LtrText(m_h.folderPath()), g_fSmallEnd.Get(), Rect(box.left + 10, box.top, box.right - 10, box.bottom), Theme::Text);
        DrawButton(Rect(r - 120, y + 80, r, y + 110), L"تغيير…", ID_FOLDER_CHANGE, false, true, Icon::Folder);
        DrawButton(Rect(r - 250, y + 80, r - 130, y + 110), L"فتح المجلد", ID_FOLDER_OPEN, false, true);
        y += 128;
    }

    // ---- 5) تشغيل تلقائي ----
    {
        Card(56);
        AddHit(Rect(L, y, R, y + 56), ID_AUTO);
        DrawToggle(l, y + 16, m_s.autoStart, true);
        Text(L"تشغيل الإعادة تلقائيًا عند فتح البرنامج", g_fBody.Get(), Rect(l + 60, y, r, y + 56), Theme::Text);
        y += 66;
    }

    m_height = y - top + 4;
}

void ReplayPanel::OnClick(int id)
{
    bool restart = true;
    if (id >= ID_SOURCE && id < ID_SOURCE + 2)          m_s.source  = id - ID_SOURCE;
    else if (id >= ID_MONITOR && id < ID_MONITOR + 4)   m_s.monitor = id - ID_MONITOR;
    else if (id >= ID_DUR && id < ID_DUR + 4)          { m_s.seconds = id - ID_DUR; }
    else if (id >= ID_RES && id < ID_RES + 3)           m_s.res     = id - ID_RES;
    else if (id >= ID_FPS && id < ID_FPS + 2)           m_s.fps     = id - ID_FPS;
    else if (id >= ID_QUALITY && id < ID_QUALITY + 3)   m_s.quality = id - ID_QUALITY;
    else if (id == ID_SYSTEM)                           m_s.system  = !m_s.system;
    else if (id == ID_MIC)                              { if (!m_h.micEnabled()) return; m_s.mic = !m_s.mic; }
    else if (id == ID_AUTO)                             { m_s.autoStart = !m_s.autoStart; restart = false; }
    else if (id == ID_FOLDER_CHANGE)                    { if (!m_h.pickFolder()) return; restart = false; }
    else if (id == ID_FOLDER_OPEN)                      { m_h.openFolder(); return; }
    else
        return;
    m_h.changed(restart);
}
