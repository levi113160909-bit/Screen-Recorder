// =============================================================
//  AddSourcePanel.cpp - نافذة "إضافة مصدر"
// =============================================================
#include "AddSourcePanel.h"
#include "AppAudio.h"
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace
{
    enum : int
    {
        ID_SEARCH_CLEAR = ID_PANEL_USER,
        ID_SEARCH_BOX,
        ID_IMAGE, ID_TEXT, ID_APP_AUDIO, ID_REGION, ID_WEB,
        ID_MONITOR_FIRST = ID_PANEL_USER + 100,
        ID_CAMERA_FIRST  = ID_PANEL_USER + 200,
        ID_WINDOW_FIRST  = ID_PANEL_USER + 300
    };

    constexpr float SEARCH_H  = 42;
    constexpr float SECTION_H = 34;
    constexpr float TILE_H    = 78;
    constexpr float ROW_H     = 56;
    constexpr float GAP       = 8;
    constexpr int   THUMB_W   = 320;     // دقة اللقطة المصغّرة

    std::wstring Lower(std::wstring s)
    {
        for (wchar_t& c : s) c = (wchar_t)std::towlower(c);
        return s;
    }

    // "الشاشة 1  •  2560×1440" -> "الشاشة 1" و "2560×1440"
    void SplitMonitorName(const std::wstring& name, std::wstring& title, std::wstring& res)
    {
        const size_t dot = name.find(L"  •  ");
        title = dot == std::wstring::npos ? name : name.substr(0, dot);
        res   = dot == std::wstring::npos ? L"" : name.substr(dot + 5);
    }
}

AddSourcePanel::AddSourcePanel(HWND exclude, AddSourceActions actions)
    : m_actions(std::move(actions))
{
    for (const CaptureTarget& t : ListCaptureTargets(exclude))
        (t.isMonitor ? m_monitors : m_windows).push_back(t);
    // الرئيسية أول
    std::stable_sort(m_monitors.begin(), m_monitors.end(),
                     [](const CaptureTarget& a, const CaptureTarget& b) { return a.isPrimary && !b.isPrimary; });
    for (const CaptureTarget& m : m_monitors)
    {
        const int mw = m.rect.right - m.rect.left, mh = m.rect.bottom - m.rect.top;
        Thumb t;
        t.w = THUMB_W;
        t.h = mw > 0 ? std::max(1, THUMB_W * mh / mw) : THUMB_W * 9 / 16;
        t.pixels = CaptureScreenThumb(m.rect, t.w, t.h);
        m_thumbs.push_back(std::move(t));
    }
    for (const CaptureTarget& w : m_windows)
        m_appNames.push_back(w.exePath.empty() ? std::wstring() : AppFriendlyName(w.exePath));
    m_cameras = ListCameras();
}

bool AddSourcePanel::Matches(const std::wstring& a, const std::wstring& b) const
{
    if (m_search.empty())
        return true;
    const std::wstring q = Lower(m_search);
    return Lower(a).find(q) != std::wstring::npos || Lower(b).find(q) != std::wstring::npos;
}

bool AddSourcePanel::OnChar(wchar_t c)
{
    if (c == 8)                       // Backspace
    {
        if (!m_search.empty())
            m_search.pop_back();
        return true;
    }
    if (c == 22)                      // Ctrl+V: لصق
    {
        if (OpenClipboard(nullptr))
        {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
                if (const wchar_t* p = (const wchar_t*)GlobalLock(h))
                {
                    std::wstring t = p;
                    GlobalUnlock(h);
                    t.erase(std::remove_if(t.begin(), t.end(), [](wchar_t ch) { return ch < 32; }), t.end());
                    m_search += t.substr(0, 60);
                }
            CloseClipboard();
        }
        return true;
    }
    if (c < 32 || m_search.size() >= 60)
        return false;
    m_search += c;
    return true;
}

float AddSourcePanel::BodyHeight() const
{
    return std::max(m_height, m_maxHeight);
}

// سطر (كاميرا أو نافذة): أيقونة يمين، عنوان وشرح، وزر إضافة يسار
float AddSourcePanel::DrawRow(float y, float L, float R, int id, ID2D1Bitmap* icon, const wchar_t* glyph,
                              const std::wstring& title, const std::wstring& sub, bool draw)
{
    if (draw)
    {
        const D2D1_RECT_F row = Rect(L, y, R, y + ROW_H);
        const bool hover = AddHit(row, id);
        Fill(row, hover ? Theme::Hover : Theme::Card, 10);
        if (hover)
            Stroke(row, WithAlpha(Theme::Accent, 0.6f), 10);

        const D2D1_RECT_F ib = Rect(R - 48, y + 12, R - 16, y + 44);
        if (icon)
            g_rt->DrawBitmap(icon, ib, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        else
        {
            Fill(ib, Theme::Hover, 8);
            Text(glyph, g_fIcon.Get(), ib, Theme::SubText);
        }
        Text(title, g_fBody.Get(), Rect(L + 110, y + 8, R - 60, y + 30), Theme::Text);
        Text(sub, g_fSmall.Get(), Rect(L + 110, y + 30, R - 60, y + 48), Theme::SubText);

        const D2D1_RECT_F action = Rect(L + 12, y + 13, L + 100, y + 43);
        Fill(action, hover ? Theme::Accent : WithAlpha(Theme::Accent, 0.14f), 8);
        Text(L"+  إضافة", g_fSegment.Get(), action, hover ? Theme::White : Theme::Accent);
    }
    return y + ROW_H + 6;
}

// -------------------------------------------------------------
//  الرسم. draw = false: بس منحسب الطول (للتمرير)
// -------------------------------------------------------------
float AddSourcePanel::Layout(float L, float y, float R, bool draw)
{
    const float top = y;
    auto Section = [&](const std::wstring& title, int count)
    {
        if (draw)
        {
            Text(title, g_fSection.Get(), Rect(L + 40, y + 8, R, y + 30), Theme::SubText);
            Text(LtrText(std::to_wstring(count)), g_fSmallEnd.Get(), Rect(L, y + 8, L + 36, y + 30), Theme::Off);
        }
        y += SECTION_H;
    };

    // ---- خانة البحث ----
    if (draw)
    {
        const D2D1_RECT_F box = Rect(L, y, R, y + SEARCH_H);
        const bool hover = AddHit(box, ID_SEARCH_BOX);
        Fill(box, Theme::Card, 10);
        Stroke(box, m_search.empty() ? (hover ? Theme::Off : Theme::Border) : Theme::Accent, 10);
        Text(L"", g_fIcon.Get(), Rect(R - 40, y, R - 12, y + SEARCH_H), Theme::SubText);   // عدسة
        const D2D1_RECT_F tr = Rect(L + 44, y, R - 44, y + SEARCH_H);
        if (m_search.empty())
            Text(L"اكتب للبحث عن نافذة أو برنامج…", g_fBody.Get(), tr, Theme::Off);
        else
        {
            Text(m_search, g_fBody.Get(), tr, Theme::Text);
            // مؤشر الكتابة (بيرمش) بآخر النص (على اليسار لأن الكتابة عربي)
            if ((m_frame / 15) % 2 == 0)
            {
                const float tw = std::min(TextWidth(m_search, g_fBody.Get()), tr.right - tr.left);
                const float cx = tr.right - tw - 2;
                Line(cx, y + 11, cx, y + SEARCH_H - 11, Theme::Accent, 1.5f);
            }
            const D2D1_RECT_F clear = Rect(L + 8, y + 7, L + 36, y + SEARCH_H - 7);
            if (AddHit(clear, ID_SEARCH_CLEAR))
                Fill(clear, Theme::Hover, 6);
            Text(Icon::Close, g_fIcon.Get(), clear, Theme::SubText);
        }
    }
    y += SEARCH_H + 10;

    // ---- إضافة سريعة (بتختفي وقت البحث) ----
    if (m_search.empty())
    {
        Section(L"إضافة سريعة", 5);
        struct Tile { int id; const wchar_t* glyph; const wchar_t* title; const wchar_t* sub; };
        const Tile tiles[] = {
            { ID_REGION,    Icon::Monitor, L"جزء من الشاشة", L"حدّد المنطقة بالسحب" },
            { ID_WEB,       L"\uE774",    L"صفحة ويب",      L"تنبيهات، دونيشن، شات" },
            { ID_IMAGE,     Icon::Photo,   L"صورة",          L"من ملف على الجهاز" },
            { ID_TEXT,      Icon::Font,    L"نص",            L"عنوان أو رسالة" },
            { ID_APP_AUDIO, Icon::Speaker, L"صوت برنامج",    L"صوت تطبيق واحد فقط" },
        };
        const float tw = (R - L - GAP * 2) / 3;
        for (int k = 0; k < 5; ++k)
        {
            const float right = R - (k % 3) * (tw + GAP);  // الأول على اليمين ، 3 بكل سطر
            const float ty = y + (k / 3) * (TILE_H + GAP);
            const D2D1_RECT_F tile = Rect(right - tw, ty, right, ty + TILE_H);
            if (!draw)
                continue;
            const bool hover = AddHit(tile, tiles[k].id);
            Fill(tile, hover ? Theme::Hover : Theme::Card, 12);
            if (hover)
                Stroke(tile, WithAlpha(Theme::Accent, 0.6f), 12);
            const D2D1_RECT_F ib = Rect(tile.right - 50, ty + 20, tile.right - 14, ty + 56);
            Fill(ib, WithAlpha(Theme::Accent, hover ? 0.28f : 0.16f), 10);
            Text(tiles[k].glyph, g_fIconBig.Get(), ib, Theme::Accent);
            Text(tiles[k].title, g_fBody.Get(), Rect(tile.left + 8, ty + 18, tile.right - 60, ty + 40), Theme::Text);
            Text(tiles[k].sub, g_fSmall.Get(), Rect(tile.left + 8, ty + 40, tile.right - 60, ty + 58), Theme::SubText);
        }
        y += TILE_H * 2 + GAP + 6;
    }

    // ---- الشاشات: بطاقات فيها لقطة، اتنين بكل سطر ----
    std::vector<int> mons;
    for (int i = 0; i < (int)m_monitors.size(); ++i)
        if (Matches(m_monitors[i].name, L"شاشة"))
            mons.push_back(i);
    if (!mons.empty())
    {
        Section(L"الشاشات", (int)mons.size());
        const float cw = (R - L - GAP) / 2;
        for (size_t n = 0; n < mons.size(); n += 2)
        {
            float rowH = 0;
            for (size_t k = n; k < std::min(n + 2, mons.size()); ++k)
            {
                const int i = mons[k];
                const float right = R - (k - n) * (cw + GAP);
                const Thumb& th = m_thumbs[i];
                const float ih = (cw - 16) * th.h / std::max(1, th.w);
                const float ch = ih + 60;
                rowH = std::max(rowH, ch);
                if (!draw)
                    continue;
                const D2D1_RECT_F card = Rect(right - cw, y, right, y + ch);
                const bool hover = AddHit(card, ID_MONITOR_FIRST + i);
                Fill(card, hover ? Theme::Hover : Theme::Card, 12);
                Stroke(card, hover ? WithAlpha(Theme::Accent, 0.7f) : Theme::Border, 12);

                const D2D1_RECT_F img = Rect(card.left + 8, y + 8, card.right - 8, y + 8 + ih);
                if (ID2D1Bitmap* bmp = m_icons.FromPixels(L"mon" + std::to_wstring(i), th.w, th.h, th.pixels))
                    g_rt->DrawBitmap(bmp, img, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                else
                    Fill(img, Theme::Background, 6);
                Stroke(img, Theme::Border, 4);

                std::wstring title, res;
                SplitMonitorName(m_monitors[i].name, title, res);
                Text(title, g_fBody.Get(), Rect(card.left + 12, y + ih + 16, card.right - 12, y + ih + 38), Theme::Text);
                Text(res, g_fSmall.Get(), Rect(card.left + 12, y + ih + 36, card.right - 12, y + ih + 54), Theme::SubText);
                if (m_monitors[i].isPrimary)
                {
                    const D2D1_RECT_F badge = Rect(card.left + 12, y + ih + 22, card.left + 76, y + ih + 44);
                    Fill(badge, WithAlpha(Theme::Accent, 0.18f), 6);
                    Text(L"الرئيسية", g_fSmallLtr.Get(), badge, Theme::Accent);
                }
            }
            y += rowH + GAP;
        }
    }

    // ---- الكاميرات ----
    std::vector<int> cams;
    for (int i = 0; i < (int)m_cameras.size(); ++i)
        if (Matches(m_cameras[i].name, L"كاميرا"))
            cams.push_back(i);
    if (!cams.empty())
    {
        Section(L"الكاميرات", (int)cams.size());
        for (int i : cams)
            y = DrawRow(y, L, R, ID_CAMERA_FIRST + i, nullptr, Icon::Camera, m_cameras[i].name, L"كاميرا ويب", draw);
    }

    // ---- النوافذ ----
    std::vector<int> wins;
    for (int i = 0; i < (int)m_windows.size(); ++i)
        if (Matches(m_windows[i].name, m_appNames[i]))
            wins.push_back(i);
    if (!wins.empty())
    {
        Section(L"النوافذ المفتوحة", (int)wins.size());
        for (int i : wins)
        {
            const CaptureTarget& w = m_windows[i];
            ID2D1Bitmap* icon = draw ? m_icons.FromWindow(w.hwnd, w.exePath) : nullptr;
            y = DrawRow(y, L, R, ID_WINDOW_FIRST + i, icon, Icon::Window, w.name,
                        m_appNames[i].empty() ? L"نافذة" : m_appNames[i], draw);
        }
    }

    if (mons.empty() && cams.empty() && wins.empty() && !m_search.empty())
    {
        if (draw)
            Text(L"لا توجد نتائج لـ «" + m_search + L"»", g_fCenter.Get(), Rect(L, y + 10, R, y + 40), Theme::SubText);
        y += 60;
    }
    return y - top + 8;
}

void AddSourcePanel::DrawBody(float L, float top, float R)
{
    ++m_frame;
    m_height = Layout(L, top, R, true);
    if (m_search.empty())
        m_maxHeight = std::max(m_maxHeight, m_height);
}

void AddSourcePanel::OnClick(int id)
{
    const AddSourceActions& a = m_actions;
    if (id == ID_SEARCH_CLEAR) { m_search.clear(); return; }
    if (id == ID_SEARCH_BOX)   return;
    if (id == ID_IMAGE)        { CloseThen(a.image); return; }
    if (id == ID_TEXT)         { CloseThen(a.text); return; }
    if (id == ID_APP_AUDIO)    { CloseThen(a.appAudio); return; }
    if (id == ID_REGION)       { if (a.region) CloseThen(a.region); return; }
    if (id == ID_WEB)          { if (a.web) CloseThen(a.web); return; }
    if (id >= ID_WINDOW_FIRST && id - ID_WINDOW_FIRST < (int)m_windows.size())
    {
        const CaptureTarget t = m_windows[id - ID_WINDOW_FIRST];
        CloseThen([fn = a.capture, t] { fn(t); });
        return;
    }
    if (id >= ID_CAMERA_FIRST && id < ID_WINDOW_FIRST && id - ID_CAMERA_FIRST < (int)m_cameras.size())
    {
        const CameraInfo c = m_cameras[id - ID_CAMERA_FIRST];
        CloseThen([fn = a.camera, c] { fn(c); });
        return;
    }
    if (id >= ID_MONITOR_FIRST && id < ID_CAMERA_FIRST && id - ID_MONITOR_FIRST < (int)m_monitors.size())
    {
        const CaptureTarget t = m_monitors[id - ID_MONITOR_FIRST];
        CloseThen([fn = a.capture, t] { fn(t); });
    }
}
