// =============================================================
//  HttpClient.cpp - طلب إنترنت بسيط (شوف HttpClient.h)
// =============================================================
#include "HttpClient.h"
#include <winhttp.h>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring UrlEncode(const std::wstring& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : ToUtf8(s))
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += (wchar_t)c;
        else
        {
            out += L'%';
            out += (wchar_t)hex[c >> 4];
            out += (wchar_t)hex[c & 15];
        }
    }
    return out;
}

HttpResponse HttpRequest(const std::wstring& method, const std::wstring& url,
                         const std::vector<std::wstring>& headers, const std::string& body, int timeoutMs)
{
    HttpResponse res;

    // تقسيم الرابط: https://host:port/path?query
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {}, extra[4096] = {};
    uc.lpszHostName = host;  uc.dwHostNameLength = (DWORD)std::size(host);
    uc.lpszUrlPath  = path;  uc.dwUrlPathLength  = (DWORD)std::size(path);
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = (DWORD)std::size(extra);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
    {
        res.error = L"الرابط غير صالح";
        return res;
    }
    const bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    const std::wstring target = std::wstring(path) + extra;

    HINTERNET session = WinHttpOpen(L"ScreenRecorder-Translator", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        res.error = L"تعذّر تجهيز الاتصال";
        return res;
    }
    const int t = timeoutMs > 0 ? timeoutMs : 15000;
    WinHttpSetTimeouts(session, std::min(t, 8000), std::min(t, 8000), t, t);

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, method.c_str(), target.c_str(), nullptr,
                                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     https ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (request)
    {
        std::wstring all;
        for (const std::wstring& h : headers)
            all += h + L"\r\n";
        const BOOL sent = WinHttpSendRequest(request, all.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : all.c_str(),
                                             all.empty() ? 0 : (DWORD)-1,
                                             body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                             (DWORD)body.size(), (DWORD)body.size(), 0);
        if (sent && WinHttpReceiveResponse(request, nullptr))
        {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            res.status = (int)status;
            res.ok = true;
            for (;;)
            {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0)
                    break;
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), avail, &read) || read == 0)
                    break;
                res.body.append(chunk.data(), read);
                if (res.body.size() > 4 * 1024 * 1024)    // ما منتوقّع أكتر من هيك
                    break;
            }
        }
        else
        {
            const DWORD err = GetLastError();
            res.error = err == ERROR_WINHTTP_TIMEOUT          ? L"انتهت مهلة الاتصال"
                      : err == ERROR_WINHTTP_CANNOT_CONNECT    ? L"تعذّر الاتصال بالخدمة"
                      : err == ERROR_WINHTTP_NAME_NOT_RESOLVED ? L"لا يوجد اتصال بالإنترنت"
                                                               : L"فشل الاتصال (" + std::to_wstring(err) + L")";
        }
    }
    else
        res.error = L"تعذّر فتح الاتصال";

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return res;
}
