// =============================================================
//  HttpClient.h - طلب إنترنت بسيط (WinHTTP)
//
//  للترجمة: منبعت النص لخدمة الترجمة (Claude ، Gemini ، DeepL...)
//  ومنستلم الجواب. بيشتغل مع https و http (مثلاً Ollama على جهازك).
//  الطلب بيوقف الخيط لحد ما يجي الجواب، فلازم ينادى من خيط بالخلفية.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

struct HttpResponse
{
    bool        ok = false;        // وصل جواب؟ (حتى لو الرقم خطأ)
    int         status = 0;        // 200 = تمام
    std::string body;              // الجواب (UTF-8)
    std::wstring error;            // سبب الفشل بالعربي (إذا ما وصل جواب)
};

// method: L"GET" أو L"POST". headers: كل سطر "Name: value" (بلا \r\n بالآخر)
// body: UTF-8. timeoutMs: أقصى مدة للطلب كله
HttpResponse HttpRequest(const std::wstring& method, const std::wstring& url,
                         const std::vector<std::wstring>& headers, const std::string& body, int timeoutMs = 15000);

// تحويل بين UTF-8 والنص العريض
std::string  ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);
// ترميز للرابط (مثلاً مفتاح بآخر الرابط)
std::wstring UrlEncode(const std::wstring& s);
