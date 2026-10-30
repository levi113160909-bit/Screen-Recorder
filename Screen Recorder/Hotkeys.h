// =============================================================
//  Hotkeys.h - اختصارات لوحة المفاتيح (بتشتغل حتى وانت باللعبة)
//
//  كل إجراء إله اختصار بتقدر تغيّره من نافذة "اختصارات لوحة المفاتيح".
//  بتنحفظ بـ settings.ini. وإذا برنامج تاني ماخد نفس الاختصار،
//  ويندوز ما بيعطينا ياه، فمنعلّم عليه "مستخدم" بالنافذة.
//
//  "الخانات": أول شي الإجراءات الثابتة (HK_RECORD...)، وبعدها خانة لكل مشهد
//  (HK_COUNT + رقم المشهد). اختصارات المشاهد بتنحفظ مع المشاهد بـ scenes.txt
//
//  الألعاب اللي شغّالة "كمسؤول" (Administrator، متل كتير ألعاب فيها مضاد غش):
//  ويندوز بيمنع البرامج العادية تشوف الأزرار وهي قدامك (لا RegisterHotKey للأزرار المفردة
//  ولا المراقب). فوقتها في "مراقب ثالث" بيقرا حالة الأزرار مباشرة كل 10 ميلي ثانية
//  (GetAsyncKeyState)، بس طول ما في نافذة "أعلى صلاحية" قدامك. ولو وصلت الكبسة من طريقين
//  main.cpp بيتجاهل التكرار (نفس الإجراء خلال ثلث ثانية).
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

// الاختصار اللي انمسك بنافذة الاختصارات (من المراقب): wParam = الزر ، lParam = Ctrl/Alt/Shift/Win
constexpr UINT WM_HOTKEY_CAPTURE = WM_APP + 40;
// اضغط لتتكلم: wParam = 1 انضغط ، 0 انترك
constexpr UINT WM_PUSH_TO_TALK = WM_APP + 41;

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
    HK_SCREENSHOT,      // لقطة شاشة (صورة PNG)
    HK_MARKER,          // علامة بنص التسجيل
    HK_PUSH_TO_TALK,    // اضغط لتتكلم (مطوّلاً): المايك بيشتغل بس وانت ضاغط
    HK_COUNT
};

struct HotkeyBinding
{
    UINT mods = 0;      // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    UINT vk   = 0;      // 0 = بلا اختصار (وفيه أزرار الفأرة: الأوسط والجانبيين)
    bool operator==(const HotkeyBinding& o) const { return mods == o.mods && vk == o.vk; }
};

// النافذة صلاحيتها أعلى من البرنامج؟ (مثلاً لعبة شغّالة كمسؤول والبرنامج عادي)
bool WindowIsMoreElevated(HWND hwnd);

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
    // الاختصار شغّال عن طريق "مراقب الأزرار" (لأن ويندوز ما قبله، أو زر فأرة) بدل تسجيله عند ويندوز
    bool ViaHook(int slot) const { return slot >= 0 && slot < (int)m_viaHook.size() && m_viaHook[slot]; }
    static bool IsMouse(UINT vk) { return vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2; }

    // التقاط اختصار جديد: أي زر بالكيبورد (حتى PrintScreen و Win) أو زر الفأرة الأوسط/الجانبي.
    // الكبسة بتنمسك قبل ويندوز وأي برنامج، وبتوصل لـ done (على خيط الواجهة)
    void BeginCapture(std::function<void(const HotkeyBinding&)> done);
    void EndCapture();
    // main.cpp بيبعتلنا رسالة WM_HOTKEY_CAPTURE
    void OnCaptureMessage(WPARAM wParam, LPARAM lParam);

    // رقم الاختصار عند ويندوز -> الخانة
    static int SlotFromId(WPARAM id) { return (int)id - 1; }

private:
    HWND          m_hwnd = nullptr;
    bool          m_suspended = false;
    std::vector<HotkeyBinding> m_keys;            // HK_COUNT + عدد المشاهد
    std::vector<bool>          m_failed;
    std::vector<bool>          m_viaHook;
    std::function<void(const HotkeyBinding&)> m_captureDone;
    void UpdateHook();
    std::vector<std::wstring>  m_sceneNames;
    int                        m_registered = 0;  // كم واحد سجّلنا (لحتى نشيلهم كلهم)
};
