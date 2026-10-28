// =============================================================
//  Net.h - اتصال إنترنت (TCP) مع تشفير اختياري (TLS)
//
//  بعض المنصات (يوتيوب، تويتش) بتقبل rtmp:// عادي،
//  وبعضها (فيسبوك، كيك) بدها rtmps:// يعني اتصال مشفّر.
//  التشفير منعمله بـ Schannel (مكتبة التشفير اللي بويندوز نفسه).
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <string>
#include <vector>

class Transport
{
public:
    Transport();
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // بيتصل بالسيرفر. tls = اتصال مشفّر
    bool Connect(const std::string& host, int port, bool tls, std::wstring& error);
    void Close();

    bool SendAll(const void* data, size_t size);

    // بيقرا لحد size بايت. بيرجّع: >0 عدد البايتات ، 0 = ما وصل شي خلال المهلة ، <0 = انقطع الاتصال
    int  Recv(void* data, size_t size, int timeoutMs);

private:
    bool TlsHandshake(const std::string& host, std::wstring& error);
    int  RecvRaw(char* data, size_t size, int timeoutMs);

    SOCKET    m_socket = INVALID_SOCKET;
    bool      m_tls = false;
    bool      m_credValid = false, m_ctxValid = false;
    CredHandle m_cred{};
    CtxtHandle m_ctx{};
    SecPkgContext_StreamSizes m_sizes{};
    std::vector<char> m_encIn;     // بيانات مشفّرة وصلت ولسا ما انفكّت
    std::vector<char> m_plainIn;   // بيانات مفكوكة جاهزة للقراءة
    std::vector<char> m_encOut;    // مكان للتشفير قبل الإرسال
};
