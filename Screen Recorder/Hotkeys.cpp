// =============================================================
//  Hotkeys.cpp - اختصارات لوحة المفاتيح (شوف Hotkeys.h)
// =============================================================
#include "Hotkeys.h"
#include <cwchar>

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
    UnregisterAll();
    m_failed.assign(m_keys.size(), false);
    m_registered = (int)m_keys.size();
    for (int i = 0; i < (int)m_keys.size(); ++i)
    {
        if (m_suspended || !m_keys[i].vk)
            continue;
        if (!RegisterHotKey(hwnd, i + 1, m_keys[i].mods | MOD_NOREPEAT, m_keys[i].vk))
            m_failed[i] = true;        // برنامج تاني ماخده
    }
}

void HotkeyManager::Suspend(bool on)
{
    if (m_suspended == on)
        return;
    m_suspended = on;
    if (on)
        UnregisterAll();
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
