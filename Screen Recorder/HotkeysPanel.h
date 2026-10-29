// =============================================================
//  HotkeysPanel.h - نافذة "اختصارات لوحة المفاتيح"
//
//  اكبس على اختصار ← اضغط المفاتيح الجديدة ← بينحفظ فوراً.
//  Esc = إلغاء ، Backspace = بلا اختصار
// =============================================================
#pragma once
#include "Panel.h"
#include "Hotkeys.h"

class HotkeysPanel : public Panel
{
public:
    // changed: بعد أي تغيير (منحفظ ومنسجّل الاختصارات من جديد)
    HotkeysPanel(HotkeyManager& mgr, std::function<void()> changed);
    ~HotkeysPanel() override;

    std::wstring   Title() const override    { return L"اختصارات لوحة المفاتيح"; }
    std::wstring   Subtitle() const override { return L"تعمل حتى داخل الألعاب وأنت خارج البرنامج"; }
    const wchar_t* IconGlyph() const override { return Icon::Keyboard; }
    float          Width() const override    { return 580; }
    float          BodyHeight() const override;
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    bool           OnKeyDown(WPARAM vk) override;
    bool           CapturingKeys() const override { return m_listen >= 0; }

private:
    void StartListening(int action);
    void OnCaptured(UINT vk, UINT mods);
    void StopListening();
    void Commit(int action, const HotkeyBinding& b);

    HotkeyManager&        m_mgr;
    std::function<void()> m_changed;
    int                   m_listen = -1;       // الإجراء اللي عم نستنى اختصاره
    std::wstring          m_note;              // رسالة صغيرة تحت (مثلاً "نُقل من ...")
    bool                  m_noteError = false;
    int                   m_frame = 0, m_noteFrame = 0;
};
