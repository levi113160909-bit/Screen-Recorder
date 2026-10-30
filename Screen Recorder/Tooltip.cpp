// =============================================================
//  Tooltip.cpp - التلميحات
// =============================================================
#include "Tooltip.h"
#include <algorithm>
#include <vector>

namespace
{
    constexpr ULONGLONG DELAY_MS = 450;          // قديش لازم يوقف الماوس قبل ما يطلع التلميح

    std::vector<std::pair<int, std::wstring>> s_tips;   // تلميحات هالرسمة
    int       s_id = 0;                          // الزر اللي الماوس فوقه
    ULONGLONG s_since = 0;                       // من إيمتى
    bool      s_dismissed = false;               // انكبس: ما منرجع نبيّنه لحتى يطلع الماوس
}

void BeginTooltips()
{
    s_tips.clear();
}

void Tip(int id, const std::wstring& text)
{
    if (id != 0 && !text.empty())
        s_tips.push_back({ id, text });
}

void DrawTooltip(float W, float H)
{
    const int id = HitAt(g_mouse);
    const ULONGLONG now = GetTickCount64();
    if (id != s_id)
    {
        s_id = id;
        s_since = now;
        s_dismissed = false;
    }
    if ((GetKeyState(VK_LBUTTON) & 0x8000) || (GetKeyState(VK_RBUTTON) & 0x8000))
        s_dismissed = true;
    if (id == 0 || s_dismissed || now - s_since < DELAY_MS)
        return;

    const std::wstring* text = nullptr;
    for (auto it = s_tips.rbegin(); it != s_tips.rend(); ++it)
        if (it->first == id) { text = &it->second; break; }
    if (!text)
        return;

    // مكان الزر (آخر واحد انسجّل بهالرقم)
    D2D1_RECT_F target = Rect(g_mouse.x, g_mouse.y, g_mouse.x, g_mouse.y);
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
        if (it->id == id) { target = it->rect; break; }

    const float tw = TextWidth(*text, g_fSmall.Get());
    const float w = std::min(tw + 22, W - 16), h = 28;
    float x = g_mouse.x - w / 2;
    float y = target.bottom + 8;
    if (y + h > H - 8)                       // ما في مكان تحت: منحطه فوق
        y = target.top - h - 8;
    x = std::clamp(x, 8.0f, std::max(8.0f, W - 8 - w));
    y = std::clamp(y, 8.0f, std::max(8.0f, H - 8 - h));

    const D2D1_RECT_F box = Rect(x, y, x + w, y + h);
    Fill(Rect(box.left - 1, box.top + 2, box.right + 1, box.bottom + 4), D2D1::ColorF(0x000000, 0.35f), 8);
    Fill(box, Theme::Card, 7);
    Stroke(box, Theme::Border, 7);
    Text(*text, g_fSmall.Get(), Rect(box.left + 10, box.top + 5, box.right - 10, box.bottom - 4), Theme::Text);
}
