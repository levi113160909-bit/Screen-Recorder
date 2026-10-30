// =============================================================
//  AppUtil.cpp - أدوات صغيرة مشتركة: رسائل وإشعارات، أسماء ملفات، تحويل إحداثيات، اختيار مجلد
// =============================================================
#include "App.h"

// في شي شغّال (تسجيل أو بث)؟ وقتها منقفل الإعدادات اللي بتأثّر عليهم
bool Busy() { return g_recorder.IsRecording() || g_streamer.IsActive(); }

// "1920×1080" مع علامات لحتى ما تنقلب الأرقام بنص عربي
std::wstring SizeText(int w, int h)
{
    return L"\u202A" + std::to_wstring(w) + L"×" + std::to_wstring(h) + L"\u202C";
}

POINT DipToScreen(float x, float y)
{
    POINT pt = { (LONG)(x * g_dpi / 96.0f), (LONG)(y * g_dpi / 96.0f) };
    ClientToScreen(g_hwnd, &pt);
    return pt;
}

std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// إشعار بزاوية الشاشة (بيبيّن لما البرنامج مش قدامك، متل وقت اللعب)
void Notify(wchar_t glyph, D2D1_COLOR_F color, const std::wstring& title, const std::wstring& sub)
{
    if (g_toastsOn)
        g_overlay.Toast(glyph, color, title, sub);
}

void ShowMessage(const std::wstring& text, bool isError)
{
    g_message        = text;
    g_messageIsError = isError;
    if (isError && !text.empty())        // الأخطاء كمان بتطلع إشعار (مثلاً وانت باللعبة)
        Notify(Glyph::Warning, Theme::Red, L"حدث خطأ", text);
}

// نص الاختصار (مثلاً "F9") للإشعارات: "اضغط F9 لـ..."
std::wstring KeyHint(int action, const std::wstring& what)
{
    const std::wstring k = HotkeyManager::Text(g_hotkeys.Get(action));
    return k.empty() ? std::wstring() : L"اضغط " + LtrText(k) + L" " + what;
}

// صندوق ويندوز لاختيار مجلد
bool PickFolder(std::wstring& path, const wchar_t* title)
{
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(title);
    CreateDirectoryW(path.c_str(), nullptr);
    ComPtr<IShellItem> start;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&start))))
        dlg->SetFolder(start.Get());
    if (FAILED(dlg->Show(g_hwnd)))
        return false;                    // انلغى
    ComPtr<IShellItem> item;
    PWSTR chosen = nullptr;
    if (FAILED(dlg->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen)))
        return false;
    path = chosen;
    CoTaskMemFree(chosen);
    return true;
}

void OpenFolderPath(const std::wstring& folder)
{
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring FormatTime(LONGLONG ms)
{
    const LONGLONG s = ms / 1000;
    wchar_t buf[32];
    swprintf_s(buf, L"%02lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}
