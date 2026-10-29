// =============================================================
//  Hotkeys.h - اختصارات لوحة المفاتيح (بتشتغل حتى وانت باللعبة)
//
//  كل إجراء إله اختصار بتقدر تغيّره من نافذة "اختصارات لوحة المفاتيح".
//  بتنحفظ بـ settings.ini. وإذا برنامج تاني ماخد نفس الاختصار،
//  ويندوز ما بيعطينا ياه، فمنعلّم عليه "مستخدم" بالنافذة.
//
//  "الخانات": أول شي الإجراءات الثابتة (HK_RECORD...)، وبعدها خانة لكل مشهد
//  (HK_COUNT + رقم المشهد). اختصارات المشاهد بتنحفظ مع المشاهد بـ scenes.txt
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

enum HotkeyAction : int
{
    HK_RECORD = 0,      // بدء/إيقاف التسجيل
    HK_PAUSE,           // إيقاف مؤقت
    HK_STREAM,          // بدء/إيقاف البث
    HK_REPLAY_SAVE,     // حفظ الإعادة
    HK_REPLAY_TOGGLE,   // تشغيل/إيقاف الإعادة
    HK_MIC_MUTE,        // كتم المايك
    HK_NEXT_SCENE,      // المشهد التالي
    HK_OVERLAY,         // إظهار/إخفاء مؤشر التسجيل
    HK_COUNT
};

struct HotkeyBinding
{
    UINT mods = 0;      // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    UINT vk   = 0;      // 0 = بلا اختصار
    bool operator==(const HotkeyBinding& o) const { return mods == o.mods && vk == o.vk; }
};

class HotkeyManager
{
public:
    HotkeyManager();

    std::wstring          Name(int slot) const;         // "بدء/إيقاف التسجيل" أو "التبديل إلى «المشهد 2»"
    static const wchar_t* ActionName(int action);
    static HotkeyBinding  Default(int slot);           // المشاهد: بلا اختصار
    static std::wstring   Text(const HotkeyBinding& b);  // "Ctrl + Shift + R" (فاضي = بلا اختصار)

    void Load(const std::wstring& iniPath);
    void Save(const std::wstring& iniPath) const;

    // بيسجّلهم كلهم عند ويندوز (أرقامهم = الإجراء + 1)
    void Apply(HWND hwnd);
    void UnregisterAll();
    // وقت تسجيل اختصار جديد بالنافذة: منوقّفهم مؤقتاً (لحتى توصلنا الكبسة نفسها)
    void Suspend(bool on);

    // المشاهد: كل ما تغيّرت (إضافة، حذف، تسمية) main.cpp بيبعتلنا اللائحة
    void SetScenes(const std::vector<std::wstring>& names, const std::vector<HotkeyBinding>& keys);
    int  SceneCount() const { return (int)m_sceneNames.size(); }
    int  Count() const      { return HK_COUNT + SceneCount(); }

    HotkeyBinding Get(int slot) const { return slot >= 0 && slot < (int)m_keys.size() ? m_keys[slot] : HotkeyBinding{}; }
    // بيرجّع رقم الخانة اللي كانت ماخدة هالاختصار (ومنشيله منها)، أو -1
    int  Set(int slot, const HotkeyBinding& b);
    bool Failed(int slot) const { return slot >= 0 && slot < (int)m_failed.size() && m_failed[slot]; }

    // رقم الاختصار عند ويندوز -> الخانة
    static int SlotFromId(WPARAM id) { return (int)id - 1; }

private:
    HWND          m_hwnd = nullptr;
    bool          m_suspended = false;
    std::vector<HotkeyBinding> m_keys;            // HK_COUNT + عدد المشاهد
    std::vector<bool>          m_failed;
    std::vector<std::wstring>  m_sceneNames;
    int                        m_registered = 0;  // كم واحد سجّلنا (لحتى نشيلهم كلهم)
};
