// =============================================================
//  Hotkeys.cpp - اختصارات لوحة المفاتيح (شوف Hotkeys.h)
// =============================================================
#include "Hotkeys.h"
#include <cwchar>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

// =============================================================
//  "مراقب الأزرار" (Low-level hook) بخيط لحالو
//  ليش؟ ويندوز ما بيقبل كل الاختصارات (مثلاً F12 لحاله، أو إذا برنامج تاني ماخده)،
//  وما بيقبل أزرار الفأرة أبداً. المراقب بيشوف كل كبسة قبل الكل:
//   - بيشغّل الاختصارات اللي ويندوز رفضها (وأزرار الفأرة)
//   - وبيمسك الاختصار الجديد بنافذة الاختصارات (أي زر بدك ياه)
//  بخيط لحالو لحتى لو الواجهة مشغولة ما يتأخر الكيبورد بكل الجهاز.
// =============================================================
namespace
{
    std::mutex                                g_hookMutex;
    std::vector<std::pair<int, HotkeyBinding>> g_hookBindings;   // الخانة ← الاختصار
    std::atomic<bool>                         g_capturing{ false };
    std::atomic<HWND>                         g_target{ nullptr };
    std::atomic<bool>                         g_hookStarted{ false };
    bool                                      g_down[256] = {};  // (بخيط المراقب بس)
    std::atomic<UINT>                         g_pttVk{ 0 };       // زر "اضغط لتتكلم" (0 = ما في)
    std::atomic<UINT>                         g_pttMods{ 0 };
    bool                                      g_pttHeld = false;
    // المراقب الثالث (للألعاب اللي شغّالة كمسؤول): كل الاختصارات، مش بس اللي عند المراقب
    std::vector<std::pair<int, HotkeyBinding>> g_pollBindings;    // (محمي بـ g_hookMutex)
    std::atomic<bool>                         g_pollStarted{ false };

    UINT ModsNow()
    {
        UINT m = 0;
        if (g_down[VK_LCONTROL] || g_down[VK_RCONTROL] || g_down[VK_CONTROL]) m |= MOD_CONTROL;
        if (g_down[VK_LMENU] || g_down[VK_RMENU] || g_down[VK_MENU])          m |= MOD_ALT;
        if (g_down[VK_LSHIFT] || g_down[VK_RSHIFT] || g_down[VK_SHIFT])       m |= MOD_SHIFT;
        if (g_down[VK_LWIN] || g_down[VK_RWIN])                               m |= MOD_WIN;
        return m;
    }
    bool IsModifierVk(UINT vk)
    {
        switch (vk)
        {
        case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LWIN: case VK_RWIN:
        case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU:
            return true;
        }
        return false;
    }

    // كبسة جديدة (مش تكرار): بترجّع true إذا لازم "نبلعها"
    bool OnPress(UINT vk)
    {
        const HWND target = g_target;
        const UINT mods = ModsNow();
        if (g_capturing)
        {
            if (IsModifierVk(vk))
                return true;                  // منبلع Ctrl/Win... (لحتى ما تفتح قائمة ابدأ مثلاً) ومنستنى الزر الأساسي
            g_capturing = false;
            if (target) PostMessageW(target, WM_HOTKEY_CAPTURE, vk, mods);
            return true;
        }
        // اضغط لتتكلم: الإضافات (Ctrl...) لازم تكون مضغوطة بالبداية بس
        if (vk == g_pttVk && (mods & g_pttMods) == g_pttMods && !g_pttHeld)
        {
            g_pttHeld = true;
            if (target) PostMessageW(target, WM_PUSH_TO_TALK, 1, 0);
        }
        std::lock_guard<std::mutex> lock(g_hookMutex);
        for (const auto& [slot, b] : g_hookBindings)
            if (b.vk == vk && b.mods == mods && target && slot != HK_PUSH_TO_TALK)
                PostMessageW(target, WM_HOTKEY, (WPARAM)(slot + 1), 0);
        return false;                         // الاختصار العادي ما منبلعه (بيوصل للعبة كمان)
    }

    void OnRelease(UINT vk)
    {
        if (g_pttHeld && vk == g_pttVk)
        {
            g_pttHeld = false;
            if (const HWND target = g_target) PostMessageW(target, WM_PUSH_TO_TALK, 0, 0);
        }
    }

    LRESULT CALLBACK KeyboardProc(int code, WPARAM wp, LPARAM lp)
    {
        if (code == HC_ACTION)
        {
            const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
            const UINT vk = k->vkCode & 0xFF;
            const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
            const bool capturing = g_capturing;
            if (down)
            {
                const bool repeat = g_down[vk];
                g_down[vk] = true;
                if (!repeat && OnPress(vk))
                    return 1;
                if (repeat && capturing)
                    return 1;
            }
            else
            {
                g_down[vk] = false;
                OnRelease(vk);
                if (capturing)
                    return 1;
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp)
    {
        if (code == HC_ACTION)
        {
            const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
            UINT vk = 0;
            if (wp == WM_MBUTTONDOWN) vk = VK_MBUTTON;
            if (wp == WM_XBUTTONDOWN) vk = HIWORD(m->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
            if (vk && OnPress(vk))
                return 1;
            if (wp == WM_MBUTTONUP) OnRelease(VK_MBUTTON);
            if (wp == WM_XBUTTONUP) OnRelease(HIWORD(m->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2);
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    // =============================================================
    //  المراقب الثالث: للنوافذ اللي صلاحيتها أعلى منّا (لعبة شغّالة كمسؤول)
    // =============================================================
    // مستوى الصلاحية (Integrity) لعملية: عادي = 0x2000 ، مسؤول = 0x3000
    DWORD IntegrityOf(HANDLE token)
    {
        DWORD len = 0;
        GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &len);
        if (!len)
            return 0;
        std::vector<BYTE> buf(len);
        if (!GetTokenInformation(token, TokenIntegrityLevel, buf.data(), len, &len))
            return 0;
        const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buf.data());
        const UCHAR count = *GetSidSubAuthorityCount(label->Label.Sid);
        return count ? *GetSidSubAuthority(label->Label.Sid, count - 1) : 0;
    }

    DWORD OwnIntegrity()
    {
        static const DWORD level = []
        {
            HANDLE t = nullptr;
            DWORD l = 0;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t))
            {
                l = IntegrityOf(t);
                CloseHandle(t);
            }
            return l;
        }();
        return level;
    }

    // النافذة اللي قدامك صلاحيتها أعلى منّا؟ (إذا ما قدرنا نعرف، متل ألعاب مضاد الغش المحمية: منعتبرها أعلى)
    bool ForegroundIsHigher(HWND fg)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (!pid || pid == GetCurrentProcessId())
            return false;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!p)
            return true;
        bool higher = true;
        HANDLE t = nullptr;
        if (OpenProcessToken(p, TOKEN_QUERY, &t))
        {
            higher = IntegrityOf(t) > OwnIntegrity();
            CloseHandle(t);
        }
        CloseHandle(p);
        return higher;
    }

    bool KeyDown(UINT vk) { return (GetAsyncKeyState((int)vk) & 0x8000) != 0; }

    UINT ModsAsync()
    {
        UINT m = 0;
        if (KeyDown(VK_CONTROL))                  m |= MOD_CONTROL;
        if (KeyDown(VK_MENU))                     m |= MOD_ALT;
        if (KeyDown(VK_SHIFT))                    m |= MOD_SHIFT;
        if (KeyDown(VK_LWIN) || KeyDown(VK_RWIN)) m |= MOD_WIN;
        return m;
    }

    void EnsurePollThread()
    {
        if (g_pollStarted.exchange(true))
            return;
        std::thread([]
        {
            bool was[256] = {};
            HWND lastFg = nullptr;
            bool higher = false, pttHeld = false;
            for (;;)
            {
                Sleep(10);
                const HWND fg = GetForegroundWindow();
                if (fg != lastFg)                  // منفحص الصلاحية بس لما تتبدّل النافذة
                {
                    lastFg = fg;
                    higher = fg && ForegroundIsHigher(fg);
                }
                std::vector<std::pair<int, HotkeyBinding>> list;
                {
                    std::lock_guard<std::mutex> lock(g_hookMutex);
                    list = g_pollBindings;
                }
                const HWND target = g_target;
                const bool active = higher && !g_capturing && target;
                const UINT mods = active ? ModsAsync() : 0;
                bool now[256] = {};
                for (const auto& [slot, b] : list)
                    now[b.vk & 0xFF] = KeyDown(b.vk);
                if (active)
                {
                    for (const auto& [slot, b] : list)
                    {
                        const UINT vk = b.vk & 0xFF;
                        if (slot == HK_PUSH_TO_TALK)
                            continue;
                        if (now[vk] && !was[vk] && b.mods == mods)       // انضغط هلق (مش مضغوط من قبل)
                            PostMessageW(target, WM_HOTKEY, (WPARAM)(slot + 1), 0);
                    }
                    // اضغط لتتكلم
                    const UINT pv = g_pttVk;
                    if (pv)
                    {
                        const bool d = KeyDown(pv) && (mods & g_pttMods) == g_pttMods;
                        if (d != pttHeld)
                        {
                            pttHeld = d;
                            PostMessageW(target, WM_PUSH_TO_TALK, d ? 1 : 0, 0);
                        }
                    }
                }
                else if (pttHeld && target)                             // رجعنا لنافذة عادية والزر لسا "مضغوط" عنّا
                {
                    pttHeld = false;
                    PostMessageW(target, WM_PUSH_TO_TALK, 0, 0);
                }
                std::memcpy(was, now, sizeof(was));
            }
        }).detach();
    }

    void EnsureHookThread()
    {
        if (g_hookStarted.exchange(true))
            return;
        std::thread([]
        {
            HHOOK kb = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
            HHOOK ms = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {}
            if (kb) UnhookWindowsHookEx(kb);
            if (ms) UnhookWindowsHookEx(ms);
        }).detach();
    }
}

bool WindowIsMoreElevated(HWND hwnd)
{
    return hwnd && ForegroundIsHigher(hwnd);
}

HotkeyManager::HotkeyManager()
{
    m_keys.resize(HK_COUNT);
    m_failed.assign(HK_COUNT, false);
    for (int i = 0; i < HK_COUNT; ++i)
        m_keys[i] = Default(i);
}

std::wstring HotkeyManager::Name(int slot) const
{
    if (slot < HK_COUNT)
        return ActionName(slot);
    const int scene = slot - HK_COUNT;
    return scene < SceneCount() ? L"التبديل إلى «" + m_sceneNames[scene] + L"»" : L"";
}

void HotkeyManager::SetScenes(const std::vector<std::wstring>& names, const std::vector<HotkeyBinding>& keys)
{
    m_sceneNames = names;
    m_keys.resize(HK_COUNT + names.size());
    for (size_t i = 0; i < names.size(); ++i)
        m_keys[HK_COUNT + i] = i < keys.size() ? keys[i] : HotkeyBinding{};
    m_failed.assign(m_keys.size(), false);
}

const wchar_t* HotkeyManager::ActionName(int action)
{
    switch (action)
    {
    case HK_RECORD:        return L"بدء التسجيل / إيقافه";
    case HK_PAUSE:         return L"إيقاف التسجيل مؤقتًا";
    case HK_STREAM:        return L"بدء البث / إيقافه";
    case HK_REPLAY_SAVE:   return L"حفظ الإعادة";
    case HK_REPLAY_TOGGLE: return L"تشغيل الإعادة / إيقافها";
    case HK_MIC_MUTE:      return L"كتم الميكروفون";
    case HK_NEXT_SCENE:    return L"المشهد التالي";
    case HK_OVERLAY:       return L"إظهار مؤشر التسجيل / إخفاؤه";
    case HK_SCREENSHOT:    return L"لقطة شاشة (صورة)";
    case HK_MARKER:        return L"وضع علامة أثناء التسجيل";
    case HK_PUSH_TO_TALK:  return L"اضغط لتتكلم (استمر بالضغط)";
    }
    return L"";
}

HotkeyBinding HotkeyManager::Default(int action)
{
    switch (action)       // المشاهد (أكبر من HK_COUNT): بلا اختصار
    {
    case HK_RECORD:      return { 0, VK_F9 };
    case HK_PAUSE:       return { 0, VK_F10 };
    case HK_STREAM:      return { 0, VK_F8 };
    case HK_REPLAY_SAVE: return { 0, VK_F7 };
    case HK_SCREENSHOT:  return { MOD_CONTROL, VK_F7 };
    case HK_MARKER:      return { MOD_CONTROL, VK_F9 };
    }
    return {};         // الباقي بلا اختصار بالأول
}

std::wstring HotkeyManager::Text(const HotkeyBinding& b)
{
    if (!b.vk)
        return L"";
    std::wstring s;
    if (b.mods & MOD_CONTROL) s += L"Ctrl + ";
    if (b.mods & MOD_ALT)     s += L"Alt + ";
    if (b.mods & MOD_SHIFT)   s += L"Shift + ";
    if (b.mods & MOD_WIN)     s += L"Win + ";

    const UINT vk = b.vk;
    if (vk == VK_MBUTTON)       return s + L"زر الفأرة الأوسط";
    if (vk == VK_XBUTTON1)      return s + L"زر الفأرة الجانبي 1";
    if (vk == VK_XBUTTON2)      return s + L"زر الفأرة الجانبي 2";
    if (vk >= VK_F1 && vk <= VK_F24)            s += L"F" + std::to_wstring(vk - VK_F1 + 1);
    else if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) s += (wchar_t)vk;
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) s += L"Num " + std::to_wstring(vk - VK_NUMPAD0);
    else
    {
        // باقي الأزرار: الاسم من ويندوز (Space, Home, Page Up...)
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        switch (vk)       // الأزرار "الموسّعة" لازم نعلّم عليها وإلا بيطلع اسم غلط
        {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_SNAPSHOT:
            scan |= 0x100;
        }
        wchar_t name[64] = {};
        if (GetKeyNameTextW((LONG)(scan << 16), name, 64) > 0)
            s += name;
        else
        {
            wchar_t hex[16];
            swprintf_s(hex, L"0x%02X", vk);
            s += hex;
        }
    }
    return s;
}

void HotkeyManager::Load(const std::wstring& ini)
{
    if (ini.empty())
        return;
    for (int i = 0; i < HK_COUNT; ++i)
    {
        wchar_t key[32], buf[64] = {};
        swprintf_s(key, L"Hotkey%d", i);
        GetPrivateProfileStringW(L"Hotkeys", key, L"", buf, 64, ini.c_str());
        if (!buf[0])
            continue;                  // ما انحفظ: الافتراضي
        UINT mods = 0, vk = 0;
        if (swscanf_s(buf, L"%u,%u", &mods, &vk) == 2)
            m_keys[i] = { mods & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN), vk & 0xFF };
    }
}

void HotkeyManager::Save(const std::wstring& ini) const
{
    if (ini.empty())
        return;
    for (int i = 0; i < HK_COUNT; ++i)       // المشاهد بتنحفظ مع المشاهد نفسها
    {
        wchar_t key[32], val[64];
        swprintf_s(key, L"Hotkey%d", i);
        swprintf_s(val, L"%u,%u", m_keys[i].mods, m_keys[i].vk);
        WritePrivateProfileStringW(L"Hotkeys", key, val, ini.c_str());
    }
}

void HotkeyManager::UnregisterAll()
{
    if (!m_hwnd)
        return;
    for (int i = 0; i < m_registered; ++i)
        UnregisterHotKey(m_hwnd, i + 1);
    m_registered = 0;
}

void HotkeyManager::Apply(HWND hwnd)
{
    m_hwnd = hwnd;
    g_target = hwnd;
    UnregisterAll();
    m_failed.assign(m_keys.size(), false);
    m_viaHook.assign(m_keys.size(), false);
    m_registered = (int)m_keys.size();
    for (int i = 0; i < (int)m_keys.size(); ++i)
    {
        if (m_suspended || !m_keys[i].vk)
            continue;
        // أزرار الفأرة، أو اللي ويندوز رفضه (برنامج تاني ماخده أو محجوز)، و"اضغط لتتكلم"
        // (بدها نعرف وقت تترك الزر كمان): عن طريق المراقب
        if (i == HK_PUSH_TO_TALK || IsMouse(m_keys[i].vk) ||
            !RegisterHotKey(hwnd, i + 1, m_keys[i].mods | MOD_NOREPEAT, m_keys[i].vk))
            m_viaHook[i] = true;
    }
    UpdateHook();
}

void HotkeyManager::UpdateHook()
{
    std::vector<std::pair<int, HotkeyBinding>> list;
    for (int i = 0; i < (int)m_keys.size(); ++i)
        if (i < (int)m_viaHook.size() && m_viaHook[i] && !m_suspended)
            list.push_back({ i, m_keys[i] });
    const bool ptt = !m_suspended && HK_PUSH_TO_TALK < (int)m_keys.size() && m_keys[HK_PUSH_TO_TALK].vk;
    g_pttMods = ptt ? m_keys[HK_PUSH_TO_TALK].mods : 0;
    g_pttVk   = ptt ? m_keys[HK_PUSH_TO_TALK].vk : 0;
    // المراقب الثالث بياخد كل الاختصارات (بيشتغل بس وقت لعبة "كمسؤول" قدامك)
    std::vector<std::pair<int, HotkeyBinding>> all;
    if (!m_suspended)
        for (int i = 0; i < (int)m_keys.size(); ++i)
            if (m_keys[i].vk)
                all.push_back({ i, m_keys[i] });
    bool any;
    {
        std::lock_guard<std::mutex> lock(g_hookMutex);
        g_hookBindings = std::move(list);
        any = !g_hookBindings.empty();
        g_pollBindings = all;
    }
    if (any || ptt)
        EnsureHookThread();
    if (!all.empty())
        EnsurePollThread();
}

void HotkeyManager::BeginCapture(std::function<void(const HotkeyBinding&)> done)
{
    m_captureDone = std::move(done);
    if (m_hwnd) g_target = m_hwnd;
    EnsureHookThread();
    g_capturing = true;
}

void HotkeyManager::EndCapture()
{
    g_capturing = false;
    m_captureDone = nullptr;
}

void HotkeyManager::OnCaptureMessage(WPARAM wParam, LPARAM lParam)
{
    auto done = m_captureDone;
    m_captureDone = nullptr;
    if (done)
        done({ (UINT)lParam, (UINT)wParam });
}

void HotkeyManager::Suspend(bool on)
{
    if (m_suspended == on)
        return;
    m_suspended = on;
    if (on)
    {
        UnregisterAll();
        UpdateHook();                  // والمراقب كمان ما بيشغّل شي
    }
    else if (m_hwnd)
        Apply(m_hwnd);
}

int HotkeyManager::Set(int action, const HotkeyBinding& b)
{
    if (action < 0 || action >= (int)m_keys.size())
        return -1;
    int moved = -1;
    if (b.vk)
        for (int i = 0; i < (int)m_keys.size(); ++i)
            if (i != action && m_keys[i] == b)
            {
                m_keys[i] = {};        // كان عند إجراء تاني: منشيله منه
                moved = i;
            }
    m_keys[action] = b;
    return moved;
}
