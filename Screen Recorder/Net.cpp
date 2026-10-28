// =============================================================
//  Net.cpp - TCP + TLS (Schannel)
// =============================================================
#include "Net.h"
#include <algorithm>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")

namespace
{
    // Winsock لازم ينفتح مرة وحدة بالبرنامج
    struct WinsockInit
    {
        WinsockInit()  { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
        ~WinsockInit() { WSACleanup(); }
    };
    WinsockInit g_winsock;

    std::wstring Widen(const std::string& s)
    {
        return std::wstring(s.begin(), s.end());
    }
}

Transport::Transport() {}

Transport::~Transport()
{
    Close();
}

void Transport::Close()
{
    if (m_ctxValid)
    {
        DeleteSecurityContext(&m_ctx);
        m_ctxValid = false;
    }
    if (m_credValid)
    {
        FreeCredentialsHandle(&m_cred);
        m_credValid = false;
    }
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_encIn.clear();
    m_plainIn.clear();
}

bool Transport::Connect(const std::string& host, int port, bool tls, std::wstring& error)
{
    Close();
    m_tls = tls;

    // 1) منلاقي عنوان السيرفر (DNS)
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0 || !result)
    {
        error = L"تعذّر العثور على السيرفر";
        return false;
    }

    // 2) منتصل
    for (addrinfo* a = result; a; a = a->ai_next)
    {
        m_socket = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (m_socket == INVALID_SOCKET)
            continue;
        if (connect(m_socket, a->ai_addr, (int)a->ai_addrlen) == 0)
            break;
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    if (m_socket == INVALID_SOCKET)
    {
        error = L"تعذّر الاتصال بالسيرفر";
        return false;
    }

    // إرسال فوري بدون تجميع + مخزن إرسال كبير (للفيديو)
    BOOL noDelay = TRUE;
    setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
    int sendBuf = 1024 * 1024;
    setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, (const char*)&sendBuf, sizeof(sendBuf));

    // 3) التشفير إذا لازم
    if (tls && !TlsHandshake(host, error))
    {
        Close();
        return false;
    }
    return true;
}

// -------------------------------------------------------------
// المصافحة المشفّرة (TLS) - السيرفر والجهاز بيتفقوا على مفتاح سرّي
// -------------------------------------------------------------
bool Transport::TlsHandshake(const std::string& host, std::wstring& error)
{
    SCHANNEL_CRED cred{};
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    cred.dwFlags   = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;

    if (AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND,
                                  nullptr, &cred, nullptr, nullptr, &m_cred, nullptr) != SEC_E_OK)
    {
        error = L"تعذّر تجهيز التشفير";
        return false;
    }
    m_credValid = true;

    std::wstring target = Widen(host);    // لحتى نتأكد إنه الشهادة إلها فعلاً
    const DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                        ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_USE_SUPPLIED_CREDS;
    DWORD outFlags = 0;

    // الخطوة الأولى: منبعت "مرحبا"
    SecBuffer outBuf{ 0, SECBUFFER_TOKEN, nullptr };
    SecBufferDesc outDesc{ SECBUFFER_VERSION, 1, &outBuf };
    SECURITY_STATUS status = InitializeSecurityContextW(&m_cred, nullptr, target.data(), flags, 0, 0,
                                                        nullptr, 0, &m_ctx, &outDesc, &outFlags, nullptr);
    if (status != SEC_I_CONTINUE_NEEDED)
    {
        error = L"فشل بدء الاتصال المشفّر";
        return false;
    }
    m_ctxValid = true;
    if (outBuf.cbBuffer > 0)
    {
        const bool ok = send(m_socket, (const char*)outBuf.pvBuffer, outBuf.cbBuffer, 0) == (int)outBuf.cbBuffer;
        FreeContextBuffer(outBuf.pvBuffer);
        if (!ok) { error = L"انقطع الاتصال أثناء التشفير"; return false; }
    }

    // الخطوات التالية: منقرا رد السيرفر ومنرد عليه لحد ما يخلص الاتفاق
    m_encIn.clear();
    bool needMore = true;
    while (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE)
    {
        if (needMore)
        {
            char tmp[16384];
            const int r = RecvRaw(tmp, sizeof(tmp), 10000);
            if (r <= 0) { error = L"السيرفر لم يرد على التشفير"; return false; }
            m_encIn.insert(m_encIn.end(), tmp, tmp + r);
        }

        SecBuffer in[2] = { { (ULONG)m_encIn.size(), SECBUFFER_TOKEN, m_encIn.data() }, { 0, SECBUFFER_EMPTY, nullptr } };
        SecBufferDesc inDesc{ SECBUFFER_VERSION, 2, in };
        SecBuffer out[1] = { { 0, SECBUFFER_TOKEN, nullptr } };
        SecBufferDesc outD{ SECBUFFER_VERSION, 1, out };

        status = InitializeSecurityContextW(&m_cred, &m_ctx, target.data(), flags, 0, 0,
                                            &inDesc, 0, nullptr, &outD, &outFlags, nullptr);

        if (status == SEC_E_INCOMPLETE_MESSAGE)
        {
            needMore = true;            // الرسالة لسا ما كملت
            continue;
        }

        if (out[0].cbBuffer > 0 && out[0].pvBuffer)
        {
            const bool ok = send(m_socket, (const char*)out[0].pvBuffer, out[0].cbBuffer, 0) == (int)out[0].cbBuffer;
            FreeContextBuffer(out[0].pvBuffer);
            if (!ok) { error = L"انقطع الاتصال أثناء التشفير"; return false; }
        }

        // في بيانات زيادة؟ منخلّيها للمرة الجاية
        if (in[1].BufferType == SECBUFFER_EXTRA && in[1].cbBuffer > 0)
        {
            std::vector<char> extra(m_encIn.end() - in[1].cbBuffer, m_encIn.end());
            m_encIn.swap(extra);
            needMore = false;
        }
        else
        {
            m_encIn.clear();
            needMore = true;
        }

        if (status == SEC_E_OK)
            break;
        if (FAILED(status))
        {
            error = L"فشل الاتصال المشفّر (الشهادة غير موثوقة؟)";
            return false;
        }
    }

    QueryContextAttributesW(&m_ctx, SECPKG_ATTR_STREAM_SIZES, &m_sizes);
    m_encOut.resize(m_sizes.cbHeader + m_sizes.cbMaximumMessage + m_sizes.cbTrailer);
    return true;
}

int Transport::RecvRaw(char* data, size_t size, int timeoutMs)
{
    if (m_socket == INVALID_SOCKET)
        return -1;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(m_socket, &set);
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    const int ready = select(0, &set, nullptr, nullptr, &tv);
    if (ready == 0)
        return 0;                       // ما وصل شي
    if (ready < 0)
        return -1;
    const int r = recv(m_socket, data, (int)size, 0);
    return r > 0 ? r : -1;              // 0 من recv = السيرفر سكّر
}

bool Transport::SendAll(const void* data, size_t size)
{
    if (m_socket == INVALID_SOCKET)
        return false;
    const char* p = (const char*)data;

    if (!m_tls)
    {
        while (size > 0)
        {
            const int s = send(m_socket, p, (int)std::min<size_t>(size, 1 << 20), 0);
            if (s <= 0) return false;
            p += s;
            size -= s;
        }
        return true;
    }

    // مشفّر: منقسّم لقطع ومنشفّر كل وحدة
    while (size > 0)
    {
        const size_t n = std::min<size_t>(size, m_sizes.cbMaximumMessage);
        char* header = m_encOut.data();
        char* body   = header + m_sizes.cbHeader;
        char* trail  = body + n;
        memcpy(body, p, n);

        SecBuffer b[4] = {
            { m_sizes.cbHeader,  SECBUFFER_STREAM_HEADER,  header },
            { (ULONG)n,          SECBUFFER_DATA,           body },
            { m_sizes.cbTrailer, SECBUFFER_STREAM_TRAILER, trail },
            { 0,                 SECBUFFER_EMPTY,          nullptr } };
        SecBufferDesc desc{ SECBUFFER_VERSION, 4, b };
        if (EncryptMessage(&m_ctx, 0, &desc, 0) != SEC_E_OK)
            return false;

        const size_t total = b[0].cbBuffer + b[1].cbBuffer + b[2].cbBuffer;
        size_t sent = 0;
        while (sent < total)
        {
            const int s = send(m_socket, header + sent, (int)(total - sent), 0);
            if (s <= 0) return false;
            sent += s;
        }
        p += n;
        size -= n;
    }
    return true;
}

int Transport::Recv(void* data, size_t size, int timeoutMs)
{
    if (!m_tls)
        return RecvRaw((char*)data, size, timeoutMs);

    // مشفّر: منفك الرسائل لحد ما يصير عنا بيانات عادية
    while (m_plainIn.empty())
    {
        if (!m_encIn.empty())
        {
            SecBuffer b[4] = { { (ULONG)m_encIn.size(), SECBUFFER_DATA, m_encIn.data() },
                               { 0, SECBUFFER_EMPTY, nullptr }, { 0, SECBUFFER_EMPTY, nullptr }, { 0, SECBUFFER_EMPTY, nullptr } };
            SecBufferDesc desc{ SECBUFFER_VERSION, 4, b };
            const SECURITY_STATUS st = DecryptMessage(&m_ctx, &desc, 0, nullptr);
            if (st == SEC_E_OK)
            {
                std::vector<char> extra;
                for (SecBuffer& x : b)
                {
                    if (x.BufferType == SECBUFFER_DATA && x.cbBuffer > 0)
                        m_plainIn.insert(m_plainIn.end(), (char*)x.pvBuffer, (char*)x.pvBuffer + x.cbBuffer);
                    if (x.BufferType == SECBUFFER_EXTRA && x.cbBuffer > 0)
                        extra.assign((char*)x.pvBuffer, (char*)x.pvBuffer + x.cbBuffer);
                }
                m_encIn.swap(extra);
                continue;
            }
            if (st == SEC_I_CONTEXT_EXPIRED)
                return -1;              // السيرفر سكّر
            if (st != SEC_E_INCOMPLETE_MESSAGE)
                return -1;
        }
        char tmp[16384];
        const int r = RecvRaw(tmp, sizeof(tmp), timeoutMs);
        if (r <= 0)
            return r;
        m_encIn.insert(m_encIn.end(), tmp, tmp + r);
    }

    const size_t n = std::min(size, m_plainIn.size());
    memcpy(data, m_plainIn.data(), n);
    m_plainIn.erase(m_plainIn.begin(), m_plainIn.begin() + n);
    return (int)n;
}
