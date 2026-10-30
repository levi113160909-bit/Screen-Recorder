// =============================================================
//  Updater.cpp - شوف Updater.h
// =============================================================
#include "Updater.h"
#include <winhttp.h>
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
        }
        if (!PostMessageW(notify, WM_UPDATE_RESULT, manual ? 1 : 0, (LPARAM)info))
            delete info;
    }).detach();
}
