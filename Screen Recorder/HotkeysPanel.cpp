// =============================================================
//  HotkeysPanel.cpp - نافذة "اختصارات لوحة المفاتيح"
// =============================================================
#include "HotkeysPanel.h"
#include <cmath>

namespace
{
    enum : int
    {
        ID_RESET_ALL = ID_PANEL_USER,
        ID_ROW_FIRST = ID_PANEL_USER + 10     // الإجراء i: +i*4 (0 الاختصار ، 1 افتراضي ، 2 حذف)
    };
    constexpr float INFO_H = 58, ROW_H = 58, GAP = 6, FOOT_H = 70, SECTION_H = 36;

    bool IsModifier(WPARAM vk)
    {
        switch (vk)
        {
        case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LWIN: case VK_RWIN:
        case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU:
            return true;
        }
        return false;
    }

    UINT CurrentMods()
    {
        UINT m = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) m |= MOD_CONTROL;
        if (GetKeyState(VK_MENU) & 0x8000)    m |= MOD_ALT;
        if (GetKeyState(VK_SHIFT) & 0x8000)   m |= MOD_SHIFT;
        if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= MOD_WIN;
        return m;
    }

    // أزرار بتنفع لحالها بدون Ctrl/Alt (ما بتنستعمل بالكتابة)
    bool OkAlone(WPARAM vk)
    {
        if (vk >= VK_F1 && vk <= VK_F24) return true;
        if (vk >= VK_BROWSER_BACK && vk <= VK_LAUNCH_APP2) return true;    // أزرار الميديا
        switch (vk)
        {
        case VK_PAUSE: case VK_SCROLL: case VK_INSERT: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT:
            return true;
        }
        return false;
    }
}

HotkeysPanel::HotkeysPanel(HotkeyManager& mgr, std::function<void()> changed)
    : m_mgr(mgr), m_changed(std::move(changed))
{
}

HotkeysPanel::~HotkeysPanel()
{
    if (m_listen >= 0)
    {
        m_mgr.EndCapture();
        m_mgr.Suspend(false);      // انسكّرت وانت عم تسجّل اختصار: منرجّعهم
    }
}

float HotkeysPanel::BodyHeight() const
{
    // قسمين: الإجراءات، والمشاهد
    return INFO_H + 10 + SECTION_H * 2 + (float)m_mgr.Count() * (ROW_H + GAP) + FOOT_H;
}

void HotkeysPanel::StartListening(int action)
{
    if (m_listen < 0)
        m_mgr.Suspend(true);       // لحتى توصلنا الكبسة بدل ما تشغّل الاختصار القديم
    m_listen = action;
    m_note.clear();
    // المراقب بيمسك أي زر (حتى PrintScreen و Win و F12 وأزرار الفأرة الجانبية)
    m_mgr.BeginCapture([this](const HotkeyBinding& b) { OnCaptured(b.vk, b.mods); });
}

void HotkeysPanel::StopListening()
{
    if (m_listen < 0)
        return;
    m_listen = -1;
    m_mgr.EndCapture();
    m_mgr.Suspend(false);
}

void HotkeysPanel::Commit(int action, const HotkeyBinding& b)
{
    const int moved = m_mgr.Set(action, b);
    StopListening();
    m_changed();                   // حفظ + تسجيل من جديد
    m_noteError = false;
    if (m_mgr.Failed(action))
    {
        m_note = L"تعذّر تفعيل هذا الاختصار، اختر غيره";
        m_noteError = true;
    }
    else if (b.vk && b.mods == 0 && !OkAlone(b.vk) && !HotkeyManager::IsMouse(b.vk))
        m_note = L"تم الحفظ • تنبيه: هذا الزر يُستخدم في الكتابة، فسيعمل الاختصار أثناء الكتابة أيضًا";
    else if (moved >= 0)
        m_note = L"نُقل الاختصار من: " + m_mgr.Name(moved);
    else
        m_note = b.vk ? L"تم الحفظ" : L"أُزيل الاختصار";
    m_noteFrame = m_frame;
}

// الزر اللي انمسك (من المراقب ، أو من الكيبورد العادي إذا المراقب ما اشتغل)
void HotkeysPanel::OnCaptured(UINT vk, UINT mods)
{
    if (m_listen < 0)
        return;
    if (vk == VK_ESCAPE && mods == 0)
    {
        StopListening();
        return;
    }
    if ((vk == VK_BACK || vk == VK_DELETE) && mods == 0)
    {
        Commit(m_listen, {});
        return;
    }
    Commit(m_listen, { mods, vk });     // أي زر بدك ياه
}

bool HotkeysPanel::OnKeyDown(WPARAM vk)
{
    if (m_listen < 0)
        return false;              // مش عم نستنى: Esc بيسكّر النافذة عادي
    if (IsModifier(vk))
        return true;               // منستنى الزر الأساسي
    OnCaptured((UINT)vk, CurrentMods());
    return true;
}

void HotkeysPanel::DrawBody(float L, float top, float R)
{
    ++m_frame;
    float y = top;

    // ---- شرح صغير ----
    const D2D1_RECT_F info = Rect(L, y, R, y + INFO_H);
    Fill(info, WithAlpha(Theme::Accent, 0.10f), 10);
    Text(L"اضغط على الاختصار، ثم أي زر أو مجموعة أزرار (أو زر الفأرة الجانبي)", g_fBody.Get(), Rect(L + 14, y + 8, R - 14, y + 30), Theme::Text);
    Text(L"Esc للإلغاء  •  Backspace لإزالة الاختصار", g_fSmall.Get(), Rect(L + 14, y + 31, R - 14, y + 50), Theme::SubText);
    y += INFO_H + 10;

    for (int i = 0; i < m_mgr.Count(); ++i)
    {
        if (i == 0 || i == HK_COUNT)        // عنوان القسم
        {
            Text(i == 0 ? L"الإجراءات" : L"المشاهد", g_fSection.Get(), Rect(L, y + 6, R, y + 28), Theme::SubText);
            if (i == HK_COUNT)
                Text(L"كل مشهد تضيفه يظهر هنا تلقائيًا", g_fSmallEnd.Get(), Rect(L, y + 6, L + 260, y + 28), Theme::Off);
            y += SECTION_H;
        }
        const int base = ID_ROW_FIRST + i * 4;
        const HotkeyBinding b = m_mgr.Get(i);
        const bool listening = (m_listen == i);
        const bool failed    = m_mgr.Failed(i) && b.vk;
        const D2D1_RECT_F row = Rect(L, y, R, y + ROW_H);
        Fill(row, listening ? WithAlpha(Theme::Accent, 0.12f) : Theme::Card, 10);
        if (listening)
            Stroke(row, Theme::Accent, 10, 1.5f);

        // الاسم (يمين)
        Text(m_mgr.Name(i), g_fBody.Get(), Rect(L + 250, y + (failed ? 8 : 0), R - 16, y + (failed ? 30 : ROW_H)), Theme::Text);
        if (failed)
            Text(L"مستخدم من برنامج آخر", g_fSmall.Get(), Rect(L + 250, y + 30, R - 16, y + 48), Theme::Red);

        // الاختصار (زر)
        const D2D1_RECT_F pill = Rect(L + 70, y + 11, L + 240, y + ROW_H - 11);
        const bool hover = AddHit(pill, base);
        if (listening)
        {
            const float a = 0.55f + 0.45f * std::sin(m_frame * 0.15f);
            Fill(pill, WithAlpha(Theme::Accent, 0.25f), 8);
            Stroke(pill, WithAlpha(Theme::Accent, a), 8, 1.5f);
            // المفاتيح المضغوطة هلق (Ctrl + Shift + ...)
            const UINT mods = CurrentMods();
            std::wstring live = HotkeyManager::Text({ mods, 'X' });
            live = mods ? live.substr(0, live.size() - 1) + L"…" : L"اضغط المفاتيح…";
            Text(mods ? LtrText(live) : live, mods ? g_fSmallLtr.Get() : g_fSegment.Get(), pill, Theme::Accent);
        }
        else
        {
            Fill(pill, hover ? Theme::Hover : Theme::Background, 8);
            Stroke(pill, failed ? Theme::Red : (hover ? Theme::Off : Theme::Border), 8);
            if (b.vk)
                Text(LtrText(HotkeyManager::Text(b)), g_fSmallLtr.Get(), pill, failed ? Theme::Red : Theme::Text);
            else
                Text(L"بلا اختصار", g_fSegment.Get(), pill, Theme::Off);
        }

        // افتراضي + حذف (يسار)
        const D2D1_RECT_F reset = Rect(L + 38, y + 14, L + 66, y + ROW_H - 14);
        const D2D1_RECT_F clear = Rect(L + 8, y + 14, L + 36, y + ROW_H - 14);
        const bool isDefault = (b == HotkeyManager::Default(i));
        if (!isDefault)
        {
            const bool h = AddHit(reset, base + 1);
            if (h) Fill(reset, Theme::Hover, 6);
            Text(Icon::Undo, g_fIcon.Get(), reset, h ? Theme::Text : Theme::SubText);
        }
        if (b.vk)
        {
            const bool h = AddHit(clear, base + 2);
            if (h) Fill(clear, D2D1::ColorF(0xE5484D, 0.2f), 6);
            Text(Icon::Close, g_fIcon.Get(), clear, h ? Theme::Red : Theme::SubText);
        }
        y += ROW_H + GAP;
    }

    // ---- تحت: رسالة + استعادة الكل ----
    y += 6;
    if (!m_note.empty() && m_frame - m_noteFrame < 150)
        Text(m_note, g_fSmall.Get(), Rect(L + 200, y, R, y + 36), m_noteError ? Theme::Yellow : Theme::Green);
    DrawButton(Rect(L, y, L + 190, y + 36), L"استعادة الافتراضي", ID_RESET_ALL, false, true, Icon::Undo);
}

void HotkeysPanel::OnClick(int id)
{
    if (id == ID_RESET_ALL)
    {
        StopListening();
        for (int i = 0; i < m_mgr.Count(); ++i)
            m_mgr.Set(i, HotkeyManager::Default(i));
        m_changed();
        m_note = L"أُعيدت كل الاختصارات إلى الافتراضي";
        m_noteError = false;
        m_noteFrame = m_frame;
        return;
    }
    const int i = (id - ID_ROW_FIRST) / 4, part = (id - ID_ROW_FIRST) % 4;
    if (i < 0 || i >= m_mgr.Count())
        return;
    if (part == 0)
    {
        if (m_listen == i) StopListening();      // كبسة تانية = إلغاء
        else               StartListening(i);
    }
    else if (part == 1)
        Commit(i, HotkeyManager::Default(i));
    else if (part == 2)
        Commit(i, {});
}
