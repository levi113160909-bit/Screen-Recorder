// =============================================================
//  Updater.cpp - شوف Updater.h
// =============================================================
#include "Updater.h"
#include <winhttp.h>
#include <algorithm>
#include <cwctype>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
    std::string HttpGet(const std::wstring& host, const std::wstring& path)
    {
        std::string body;
        HINTERNET s = WinHttpOpen(L"ScreenRecorder-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) return body;
        WinHttpSetTimeouts(s, 5000, 5000, 8000, 8000);
        HINTERNET c = WinHttpConnect(s, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\nUser-Agent: ScreenRecorder\r\n";
        if (r && WinHttpSendRequest(r, headers, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, nullptr))
        {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr);
            if (status == 200)
                for (;;)
                {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0) break;
                    std::vector<char> buf(avail);
                    DWORD read = 0;
                    if (!WinHttpReadData(r, buf.data(), avail, &read) || read == 0) break;
                    body.append(buf.data(), read);
                    if (body.size() > 2 * 1024 * 1024) break;
                }
        }
        if (r) WinHttpCloseHandle(r);
        if (c) WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
        return body;
    }

    // قيمة نص بسيطة من JSON: "key": "value"
    std::string JsonString(const std::string& json, const std::string& key)
    {
        const std::string k = "\"" + key + "\"";
        size_t p = json.find(k);
        if (p == std::string::npos) return {};
        p = json.find(':', p + k.size());
        if (p == std::string::npos) return {};
        p = json.find('"', p);
        if (p == std::string::npos) return {};
        const size_t e = json.find('"', p + 1);
        return e == std::string::npos ? std::string() : json.substr(p + 1, e - p - 1);
    }

    // رقم من JSON: "key": 123
    ULONGLONG JsonNumber(const std::string& json, const std::string& key)
    {
        const std::string k = "\"" + key + "\"";
        size_t p = json.find(k);
        if (p == std::string::npos) return 0;
        p = json.find(':', p + k.size());
        if (p == std::string::npos) return 0;
        ++p;
        while (p < json.size() && json[p] == ' ') ++p;
        ULONGLONG v = 0;
        while (p < json.size() && json[p] >= '0' && json[p] <= '9')
            v = v * 10 + (json[p++] - '0');
        return v;
    }

    // عناصر مصفوفة "assets" (كل ملف بالإصدار) كنصوص JSON منفصلة
    std::vector<std::string> AssetObjects(const std::string& json)
    {
        std::vector<std::string> out;
        size_t p = json.find("\"assets\"");
        if (p == std::string::npos) return out;
        p = json.find('[', p);
        if (p == std::string::npos) return out;
        int depth = 0;
        bool inStr = false;
        size_t start = 0;
        for (size_t i = p + 1; i < json.size(); ++i)
        {
            const char ch = json[i];
            if (inStr)
            {
                if (ch == '\\') ++i;
                else if (ch == '"') inStr = false;
                continue;
            }
            if (ch == '"') inStr = true;
            else if (ch == '{') { if (depth++ == 0) start = i; }
            else if (ch == '}') { if (--depth == 0) out.push_back(json.substr(start, i - start + 1)); }
            else if (ch == ']' && depth == 0) break;
        }
        return out;
    }

    std::wstring Widen(const std::string& s)
    {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

    std::vector<int> Parts(std::wstring v)
    {
        while (!v.empty() && (v[0] == L'v' || v[0] == L'V')) v.erase(0, 1);
        std::vector<int> out;
        int cur = 0; bool any = false;
        for (wchar_t c : v)
        {
            if (c >= L'0' && c <= L'9') { cur = cur * 10 + (c - L'0'); any = true; }
            else if (c == L'.') { out.push_back(cur); cur = 0; any = false; }
            else break;                    // "-beta" وغيره منتجاهله
        }
        if (any || !out.empty()) out.push_back(cur);
        return out;
    }
}

bool IsNewerVersion(const std::wstring& latest, const std::wstring& current)
{
    std::vector<int> a = Parts(latest), b = Parts(current);
    a.resize(4, 0); b.resize(4, 0);
    for (int i = 0; i < 4; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

void CheckForUpdate(HWND notify, const std::wstring& repo, const std::wstring& currentVersion, bool manual)
{
    std::thread([notify, repo, currentVersion, manual]
    {
        auto* info = new UpdateInfo();
        const std::string json = HttpGet(L"api.github.com", L"/repos/" + repo + L"/releases/latest");
        const std::string tag = JsonString(json, "tag_name");
        if (!tag.empty())
        {
            info->ok      = true;
            info->version = Widen(tag);
            while (!info->version.empty() && (info->version[0] == L'v' || info->version[0] == L'V'))
                info->version.erase(0, 1);
            info->url     = Widen(JsonString(json, "html_url"));
            info->newer   = IsNewerVersion(info->version, currentVersion);
            // ملف المثبّت بين ملفات الإصدار: ScreenRecorder-Setup-*.exe
            for (const std::string& a : AssetObjects(json))
            {
                const std::string name = JsonString(a, "name");
                if (name.rfind("ScreenRecorder-Setup-", 0) != 0 || name.size() < 5 ||
                    _stricmp(name.c_str() + name.size() - 4, ".exe") != 0)
                    continue;
                info->setupName = Widen(name);
                info->setupUrl  = Widen(JsonString(a, "browser_download_url"));
                info->setupSize = JsonNumber(a, "size");
                const std::string digest = JsonString(a, "digest");          // "sha256:abc..."
                if (digest.rfind("sha256:", 0) == 0)
                    info->setupSha256 = Widen(digest.substr(7));
                break;
            }
        }
        if (!PostMessageW(notify, WM_UPDATE_RESULT, manual ? 1 : 0, (LPARAM)info))
            delete info;
    }).detach();
}

// =============================================================
//  التنزيل والتثبيت
// =============================================================
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "bcrypt.lib")

std::atomic<int> g_updateDownloadPercent{ -1 };

bool UpdateDownloading() { return g_updateDownloadPercent >= 0; }

namespace
{
    constexpr ULONGLONG MAX_SETUP_SIZE = 300ull * 1024 * 1024;     // أكبر من هيك: أكيد في غلط

    // تنزيل ملف لـ path (بيتبع التحويلات: GitHub بيحوّل لخادم الملفات تبعه)
    bool HttpDownload(const std::wstring& url, const std::wstring& path, ULONGLONG expected)
    {
        URL_COMPONENTS uc{ sizeof(uc) };
        wchar_t host[256] = {}, urlPath[2048] = {};
        uc.lpszHostName = host;    uc.dwHostNameLength = 256;
        uc.lpszUrlPath  = urlPath; uc.dwUrlPathLength  = 2048;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS)
            return false;                                   // HTTPS بس
        bool ok = false;
        HINTERNET s = WinHttpOpen(L"ScreenRecorder-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) return false;
        WinHttpSetTimeouts(s, 10000, 10000, 30000, 30000);
        HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", urlPath, nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        if (r)
        {
            DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
            WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        }
        const wchar_t* headers = L"Accept: application/octet-stream\r\nUser-Agent: ScreenRecorder\r\n";
        if (r && WinHttpSendRequest(r, headers, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, nullptr))
        {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr);
            HANDLE f = status == 200 ? CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                                   FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
            if (f != INVALID_HANDLE_VALUE)
            {
                ULONGLONG total = 0;
                std::vector<char> buf(256 * 1024);
                ok = true;
                for (;;)
                {
                    DWORD read = 0;
                    if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &read)) { ok = false; break; }
                    if (read == 0) break;
                    DWORD written = 0;
                    if (!WriteFile(f, buf.data(), read, &written, nullptr) || written != read) { ok = false; break; }
                    total += read;
                    if (total > MAX_SETUP_SIZE) { ok = false; break; }
                    if (expected)
                        g_updateDownloadPercent = (int)std::min<ULONGLONG>(99, total * 100 / expected);
                }
                CloseHandle(f);
                if (expected && total != expected)
                    ok = false;
                if (!ok)
                    DeleteFileW(path.c_str());
            }
        }
        if (r) WinHttpCloseHandle(r);
        if (c) WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
        return ok;
    }

    bool SafeFileName(const std::wstring& n)
    {
        if (n.empty() || n.size() > 120) return false;
        for (wchar_t ch : n)
            if (!(iswalnum(ch) || ch == L'-' || ch == L'_' || ch == L'.')) return false;
        return n.find(L"..") == std::wstring::npos;
    }
}

std::wstring UpdatesFolder()
{
    std::wstring dir;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
    {
        dir = std::wstring(local) + L"\\ScreenRecorder\\Updates";
        CoTaskMemFree(local);
    }
    return dir;
}

void ClearUpdatesFolder()
{
    const std::wstring dir = UpdatesFolder();
    if (dir.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool FileSha256Matches(const std::wstring& path, const std::wstring& hex)
{
    if (hex.size() != 64)
        return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    UCHAR digest[32] = {};
    bool ok = false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE &&
        BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)))
    {
        std::vector<UCHAR> buf(256 * 1024);
        ok = true;
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(f, buf.data(), (DWORD)buf.size(), &read, nullptr)) { ok = false; break; }
            if (!read) break;
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buf.data(), read, 0))) { ok = false; break; }
        }
        ok = ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok)
        return false;
    wchar_t out[65] = {};
    for (int i = 0; i < 32; ++i)
        swprintf_s(out + i * 2, 3, L"%02x", digest[i]);
    return _wcsicmp(out, hex.c_str()) == 0;
}

void DownloadUpdate(HWND notify, const UpdateInfo& info, const std::wstring& folder)
{
    if (UpdateDownloading() || info.setupUrl.empty() || !SafeFileName(info.setupName))
        return;
    g_updateDownloadPercent = 0;
    const UpdateInfo copy = info;
    std::thread([notify, copy, folder]
    {
        CreateDirectoryW(folder.c_str(), nullptr);
        const std::wstring final = folder + L"\\" + copy.setupName;
        const std::wstring part  = final + L".part";
        bool ok = HttpDownload(copy.setupUrl, part, copy.setupSize);
        // البصمة: إذا GitHub عطانا ياها لازم تطابق (وإلا الملف خربان أو معدّل)
        if (ok && !copy.setupSha256.empty() && !FileSha256Matches(part, copy.setupSha256))
            ok = false;
        if (ok)
        {
            DeleteFileW(final.c_str());
            ok = MoveFileW(part.c_str(), final.c_str()) != FALSE;
        }
        DeleteFileW(part.c_str());
        g_updateDownloadPercent = -1;
        auto* path = ok ? new std::wstring(final) : nullptr;
        if (!PostMessageW(notify, WM_UPDATE_DOWNLOAD, ok ? 1 : 0, (LPARAM)path))
            delete path;
    }).detach();
}

bool IsInstalledCopy()
{
    static const bool installed = []
    {
    // المثبّت (Inno Setup) بيكتب مكان التثبيت هون (نفس AppId بـ installer\ScreenRecorder.iss)
    const wchar_t KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{34FF91E3-4127-4C82-ABD6-C44B9AEA8F35}_is1";
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    std::wstring dir = exe;
    dir.resize(dir.find_last_of(L'\\'));
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE })
    {
        wchar_t loc[MAX_PATH * 2] = {};
        DWORD size = sizeof(loc);
        if (RegGetValueW(root, KEY, L"InstallLocation", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, loc, &size) != ERROR_SUCCESS)
            continue;
        std::wstring l = loc;
        while (!l.empty() && (l.back() == L'\\' || l.back() == L'/'))
            l.pop_back();
        if (_wcsicmp(l.c_str(), dir.c_str()) == 0)
            return true;
    }
    return false;
    }();                 // مكان البرنامج ما بيتغيّر وهو شغّال: منفحص مرة وحدة
    return installed;
}

bool RunSetup(const std::wstring& setupPath, bool silent, bool tray)
{
    // الصامت: بلا نوافذ ولا أسئلة ، لنفس المستخدم ، وبيعيد فتح البرنامج بآخره (installer: {param:relaunch})
    const std::wstring args = silent
        ? std::wstring(L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CURRENTUSER /relaunch=") + (tray ? L"tray" : L"normal")
        : std::wstring();
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask        = SEE_MASK_NOASYNC;
    sei.lpVerb       = L"open";
    sei.lpFile       = setupPath.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.nShow        = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}
