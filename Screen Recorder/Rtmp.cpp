// =============================================================
//  Rtmp.cpp - بروتوكول RTMP (اللي بتستعمله منصات البث)
//
//  الخطوات مع كل منصة:
//   1) مصافحة (handshake)
//   2) connect      -> "بدي اتصل بالتطبيق live2 مثلاً"
//   3) createStream -> "اعطيني قناة"
//   4) publish      -> "هاد مفتاح البث تبعي، بدي ابث"
//   5) معلومات الفيديو + إعدادات الفك + الإطارات وحدة ورا التانية
// =============================================================
#include "Net.h"            // لازم أول شي (winsock2 قبل windows.h)
#include "Rtmp.h"
#include "Bitstream.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace
{
    // ---------------------------------------------------------
    //  AMF0: طريقة RTMP لكتابة الأوامر (أرقام ونصوص وكائنات)
    // ---------------------------------------------------------
    struct Amf
    {
        std::vector<uint8_t> b;

        void U16(uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
        void U32(uint32_t v) { for (int i = 3; i >= 0; --i) b.push_back(uint8_t(v >> (i * 8))); }
        void Key(const std::string& k) { U16((uint16_t)k.size()); b.insert(b.end(), k.begin(), k.end()); }

        Amf& Num(double d)
        {
            b.push_back(0x00);
            uint64_t v;
            memcpy(&v, &d, 8);
            for (int i = 7; i >= 0; --i) b.push_back(uint8_t(v >> (i * 8)));
            return *this;
        }
        Amf& Bool(bool v) { b.push_back(0x01); b.push_back(v ? 1 : 0); return *this; }
        Amf& Str(const std::string& s)
        {
            if (s.size() > 0xFFFF) { b.push_back(0x0C); U32((uint32_t)s.size()); }
            else                   { b.push_back(0x02); U16((uint16_t)s.size()); }
            b.insert(b.end(), s.begin(), s.end());
            return *this;
        }
        Amf& Null() { b.push_back(0x05); return *this; }
        Amf& BeginObject() { b.push_back(0x03); return *this; }
        Amf& BeginEcma(uint32_t count) { b.push_back(0x08); U32(count); return *this; }
        Amf& End() { b.push_back(0); b.push_back(0); b.push_back(0x09); return *this; }
        Amf& PropNum(const std::string& k, double v)       { Key(k); return Num(v); }
        Amf& PropStr(const std::string& k, const std::string& v) { Key(k); return Str(v); }
        Amf& PropBool(const std::string& k, bool v)        { Key(k); return Bool(v); }
        Amf& PropStrings(const std::string& k, const std::vector<std::string>& list)   // Strict Array
        {
            Key(k);
            b.push_back(0x0A);
            U32((uint32_t)list.size());
            for (const std::string& s : list) Str(s);
            return *this;
        }
    };

    // قراءة AMF0 (اللي بيرجّعه السيرفر)
    struct AmfValue
    {
        int                   type = 6;     // 6 = undefined
        double                num = 0;
        bool                  boolean = false;
        std::string           str;
        std::vector<std::string> keys;
        std::vector<AmfValue> values;

        const AmfValue* Get(const std::string& k) const
        {
            for (size_t i = 0; i < keys.size(); ++i)
                if (keys[i] == k) return &values[i];
            return nullptr;
        }
        std::string GetStr(const std::string& k) const
        {
            const AmfValue* v = Get(k);
            return v ? v->str : std::string();
        }
    };

    struct AmfReader
    {
        const uint8_t* p;
        const uint8_t* end;

        bool Need(size_t n) const { return (size_t)(end - p) >= n; }
        uint16_t U16() { uint16_t v = uint16_t(p[0] << 8 | p[1]); p += 2; return v; }
        uint32_t U32() { uint32_t v = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; p += 4; return v; }

        bool ReadString(std::string& s, bool longStr)
        {
            if (!Need(longStr ? 4 : 2)) return false;
            const uint32_t n = longStr ? U32() : U16();
            if (!Need(n)) return false;
            s.assign((const char*)p, n);
            p += n;
            return true;
        }

        bool ReadProps(AmfValue& v)
        {
            while (true)
            {
                if (!Need(3)) return false;
                if (p[0] == 0 && p[1] == 0 && p[2] == 0x09) { p += 3; return true; }
                std::string k;
                if (!ReadString(k, false)) return false;
                AmfValue child;
                if (!Read(child)) return false;
                v.keys.push_back(k);
                v.values.push_back(std::move(child));
            }
        }

        bool Read(AmfValue& v)
        {
            if (!Need(1)) return false;
            v.type = *p++;
            switch (v.type)
            {
            case 0x00:   // رقم
            {
                if (!Need(8)) return false;
                uint64_t x = 0;
                for (int i = 0; i < 8; ++i) x = (x << 8) | p[i];
                p += 8;
                memcpy(&v.num, &x, 8);
                return true;
            }
            case 0x01: if (!Need(1)) return false; v.boolean = *p++ != 0; return true;
            case 0x02: return ReadString(v.str, false);
            case 0x0C: return ReadString(v.str, true);
            case 0x03: return ReadProps(v);
            case 0x08: if (!Need(4)) return false; U32(); return ReadProps(v);
            case 0x05: case 0x06: return true;
            case 0x0A:   // مصفوفة
            {
                if (!Need(4)) return false;
                const uint32_t n = U32();
                for (uint32_t i = 0; i < n; ++i)
                {
                    AmfValue child;
                    if (!Read(child)) return false;
                    v.values.push_back(std::move(child));
                }
                return true;
            }
            case 0x0B: if (!Need(10)) return false; p += 10; return true;   // تاريخ
            default: return false;
            }
        }
    };

    std::vector<AmfValue> DecodeAll(const uint8_t* data, size_t len)
    {
        std::vector<AmfValue> out;
        AmfReader r{ data, data + len };
        while (r.p < r.end)
        {
            AmfValue v;
            if (!r.Read(v)) break;
            out.push_back(std::move(v));
        }
        return out;
    }

    std::string Utf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    std::wstring Wide(const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

    std::string Trim(std::string s)
    {
        while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
        size_t i = 0;
        while (i < s.size() && isspace((unsigned char)s[i])) ++i;
        return s.substr(i);
    }

    // rtmp://host:port/app  ->  أجزاؤه
    struct ParsedUrl
    {
        bool        tls = false;
        std::string host, app, tcUrl;
        int         port = 1935;
    };

    bool ParseUrl(const std::wstring& wurl, ParsedUrl& out)
    {
        std::string url = Trim(Utf8(wurl));
        std::string lower = url;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });

        size_t pos;
        if (lower.rfind("rtmps://", 0) == 0)      { out.tls = true;  out.port = 443;  pos = 8; }
        else if (lower.rfind("rtmp://", 0) == 0)  { out.tls = false; out.port = 1935; pos = 7; }
        else return false;

        const size_t slash = url.find('/', pos);
        std::string hostPort = url.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        out.app = slash == std::string::npos ? "" : url.substr(slash + 1);
        while (!out.app.empty() && out.app.back() == '/')
            out.app.pop_back();

        const size_t colon = hostPort.rfind(':');
        if (colon != std::string::npos && hostPort.find(']') == std::string::npos)
        {
            out.port = atoi(hostPort.substr(colon + 1).c_str());
            out.host = hostPort.substr(0, colon);
        }
        else
            out.host = hostPort;
        if (out.host.empty() || out.port <= 0)
            return false;

        out.tcUrl = (out.tls ? "rtmps://" : "rtmp://") + hostPort + "/" + out.app;
        return true;
    }

    constexpr uint8_t CS_CONTROL = 2, CS_COMMAND = 3, CS_AUDIO = 4, CS_VIDEO = 6, CS_DATA = 5;
    constexpr uint8_t MSG_CHUNK_SIZE = 1, MSG_ABORT = 2, MSG_ACK = 3, MSG_USER = 4, MSG_WINDOW = 5, MSG_PEER_BW = 6,
                      MSG_AUDIO = 8, MSG_VIDEO = 9, MSG_DATA_AMF3 = 15, MSG_CMD_AMF3 = 17, MSG_DATA = 18, MSG_CMD = 20;

    double NowSeconds()
    {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }
}

// =============================================================
//  المنصات
// =============================================================
std::wstring DefaultServer(Platform p)
{
    switch (p)
    {
    case Platform::YouTube:  return L"rtmp://a.rtmp.youtube.com/live2";
    case Platform::Twitch:   return L"rtmp://live.twitch.tv/app";
    case Platform::Facebook: return L"rtmps://live-api-s.facebook.com:443/rtmp/";
    default:                 return L"";      // كيك وتيك توك: كل حساب إلو سيرفر خاص
    }
}

std::wstring PlatformName(Platform p)
{
    switch (p)
    {
    case Platform::YouTube:  return L"يوتيوب";
    case Platform::Twitch:   return L"تويتش";
    case Platform::Facebook: return L"فيسبوك";
    case Platform::Kick:     return L"كيك";
    case Platform::TikTok:   return L"تيك توك";
    default:                 return L"مخصّص";
    }
}

// =============================================================
//  RtmpOutput
// =============================================================
RtmpOutput::RtmpOutput(const StreamTarget& target, const StreamInfo& info, ConfigGetter avcConfig,
                       std::function<void()> requestKeyframe)
    : m_target(target), m_info(info), m_getConfig(std::move(avcConfig)),
      m_requestKeyframe(std::move(requestKeyframe)), m_t(std::make_unique<Transport>())
{
}

RtmpOutput::~RtmpOutput()
{
    Stop();
}

void RtmpOutput::Start()
{
    if (m_running) return;
    m_running = true;
    m_thread = std::thread(&RtmpOutput::ThreadMain, this);
}

void RtmpOutput::Stop()
{
    m_running = false;
    m_qCv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    SetState(OutputState::Idle);
}

void RtmpOutput::SetState(OutputState s, const std::wstring& message)
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_status.state   = s;
    m_status.message = message;
    if (s != OutputState::Live)
        m_status.kbps = 0;
}

OutputStatus RtmpOutput::Status()
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    OutputStatus s = m_status;
    s.dropped = m_dropped;
    return s;
}

// القطع بتوصل من المشفّر (نفس القطعة بتروح لكل المنصات)
void RtmpOutput::Push(const std::shared_ptr<const EncodedPacket>& pkt)
{
    if (!m_live)
        return;                         // لسا ما اتصلنا: منرمي

    bool needKey = false;
    {
        std::lock_guard<std::mutex> lock(m_qMutex);
        m_queue.push_back(pkt);

        // النت أبطأ من البث؟ إذا تجمّع أكتر من ثانيتين منرمي الكل
        // ومنستنى إطار كامل جديد (أحسن من تأخير بيكبر للأبد)
        const uint32_t span = m_queue.back()->timeMs - m_queue.front()->timeMs;
        if (m_queue.size() > 8 && span > 2000 && span < 0x80000000u)
        {
            uint32_t videoDropped = 0;
            for (auto& q : m_queue)
                if (q->video) ++videoDropped;
            m_dropped += videoDropped;
            m_queue.clear();
            m_waitKeyframe = true;
            needKey = true;
        }
    }
    m_qCv.notify_one();
    if (needKey && m_requestKeyframe)
        m_requestKeyframe();
}

void RtmpOutput::ThreadMain()
{
    bool first = true;
    while (m_running)
    {
        SetState(first ? OutputState::Connecting : OutputState::Reconnecting);
        first = false;
        m_fatal = false;

        std::wstring error;
        Session(error);

        m_live = false;
        m_t->Close();
        {
            std::lock_guard<std::mutex> lock(m_qMutex);
            m_queue.clear();
        }
        if (!m_running)
            break;

        // المنصة رفضت (مفتاح غلط مثلاً): منبيّن الخطأ ومنطوّل أكتر قبل ما نعيد
        SetState(m_fatal ? OutputState::Failed : OutputState::Reconnecting, error);
        const int waitMs = m_fatal ? 15000 : 3000;
        for (int t = 0; t < waitMs && m_running; t += 100)
            Sleep(100);
    }
}

// اتصال واحد كامل، بيرجع لما ينقطع أو نوقّف
bool RtmpOutput::Session(std::wstring& error)
{
    ParsedUrl url;
    if (!ParseUrl(m_target.url, url))
    {
        error = L"عنوان الخادم غير صالح";
        m_fatal = true;
        return false;
    }
    const std::string key = Trim(Utf8(m_target.key));
    if (key.empty())
    {
        error = L"مفتاح البث فارغ";
        m_fatal = true;
        return false;
    }

    // تصفير الحالة
    m_outChunk = 128; m_inChunk = 128; m_streamId = 1;
    m_received = 0; m_lastAck = 0; m_window = 2500000;
    m_rbuf.clear(); m_in.clear();
    m_videoHeaderSent = false; m_haveBase = false; m_baseMs = 0;
    m_waitKeyframe = true;

    if (!m_t->Connect(url.host, url.port, url.tls, error))
        return false;
    if (!Handshake())
    {
        error = L"فشلت المصافحة مع الخادم";
        return false;
    }
    if (!SendChunkSize(4096))
    {
        error = L"انقطع الاتصال";
        return false;
    }

    // 1) connect
    {
        Amf a;
        a.Str("connect").Num(1).BeginObject()
            .PropStr("app", url.app)
            .PropStr("type", "nonprivate")
            .PropStr("flashVer", "FMLE/3.0 (compatible; FMSc/1.0)")
            .PropStr("swfUrl", url.tcUrl)
            .PropStr("tcUrl", url.tcUrl);
        if (m_info.hevc)
            a.PropStrings("fourCcList", { "hvc1" });        // Enhanced RTMP: منخبّر الخادم إنو رح نبعت HEVC
        a.End();
        if (!SendCommand(CS_COMMAND, 0, a.b) || !WaitForResult(1, nullptr, error, 10000))
        {
            if (error.empty()) error = L"رفض الخادم الاتصال";
            return false;
        }
    }

    // 2) releaseStream + FCPublish (بعض المنصات بدها ياهم) + createStream
    {
        Amf r; r.Str("releaseStream").Num(2).Null().Str(key);
        Amf f; f.Str("FCPublish").Num(3).Null().Str(key);
        Amf c; c.Str("createStream").Num(4).Null();
        if (!SendCommand(CS_COMMAND, 0, r.b) || !SendCommand(CS_COMMAND, 0, f.b) || !SendCommand(CS_COMMAND, 0, c.b))
        {
            error = L"انقطع الاتصال";
            return false;
        }
        double id = 1;
        if (!WaitForResult(4, &id, error, 10000))
        {
            if (error.empty()) error = L"تعذّر إنشاء البث";
            return false;
        }
        m_streamId = id > 0 ? (uint32_t)id : 1;
    }

    // 3) publish
    {
        Amf p; p.Str("publish").Num(5).Null().Str(key).Str("live");
        if (!SendCommand(CS_DATA, m_streamId, p.b))
        {
            error = L"انقطع الاتصال";
            return false;
        }
        if (!WaitForPublish(error, 10000))
            return false;
    }

    // 4) معلومات البث
    {
        Amf m;
        m.Str("@setDataFrame").Str("onMetaData").BeginEcma(12)
            .PropNum("duration", 0)
            .PropNum("width", m_info.width)
            .PropNum("height", m_info.height)
            .PropNum("videodatarate", m_info.videoKbps)
            .PropNum("framerate", m_info.fps)
            .PropNum("videocodecid", bs::FlvVideoCodecId(m_info.hevc))
            .PropNum("audiodatarate", m_info.audioKbps)
            .PropNum("audiosamplerate", 48000)
            .PropNum("audiosamplesize", 16)
            .PropBool("stereo", true)
            .PropNum("audiocodecid", 10)
            .PropStr("encoder", "Screen Recorder")
            .End();
        if (!SendRtmp(CS_DATA, MSG_DATA, 0, m_streamId, m.b.data(), m.b.size()))
        {
            error = L"انقطع الاتصال";
            return false;
        }
    }

    // 5) إعدادات فك الصوت
    {
        std::vector<uint8_t> a = { 0xAF, 0x00 };
        const auto cfg = AacEncoder::AudioConfig();
        a.insert(a.end(), cfg.begin(), cfg.end());
        if (!SendRtmp(CS_AUDIO, MSG_AUDIO, 0, m_streamId, a.data(), a.size()))
        {
            error = L"انقطع الاتصال";
            return false;
        }
    }

    // صرنا عالهوا!
    SetState(OutputState::Live);
    m_live = true;
    if (m_requestKeyframe)
        m_requestKeyframe();           // المشاهد بيحتاج إطار كامل ليبلّش

    double   windowStart = NowSeconds();
    uint64_t windowBytes = m_bytesSent;

    while (m_running)
    {
        std::shared_ptr<const EncodedPacket> pkt;
        {
            std::unique_lock<std::mutex> lock(m_qMutex);
            m_qCv.wait_for(lock, std::chrono::milliseconds(50), [&] { return !m_queue.empty() || !m_running; });
            if (!m_queue.empty())
            {
                pkt = m_queue.front();
                m_queue.pop_front();
            }
        }

        if (pkt && !SendPacket(*pkt))
        {
            error = L"انقطع الاتصال بالخادم";
            return true;
        }
        if (!PollIncoming())
        {
            error = L"أغلق الخادم الاتصال";
            return true;
        }

        // السرعة كل ثانية
        const double now = NowSeconds();
        if (now - windowStart >= 1.0)
        {
            const double kbps = (m_bytesSent - windowBytes) * 8.0 / 1000.0 / (now - windowStart);
            {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_status.kbps = kbps;
            }
            windowStart = now;
            windowBytes = m_bytesSent;
        }
    }

    // وداعاً بأدب
    Amf u; u.Str("FCUnpublish").Num(6).Null().Str(key);
    Amf d; d.Str("deleteStream").Num(7).Null().Num(m_streamId);
    SendCommand(CS_COMMAND, 0, u.b);
    SendCommand(CS_COMMAND, 0, d.b);
    return true;
}

// -------------------------------------------------------------
//  إرسال قطعة فيديو أو صوت
// -------------------------------------------------------------
bool RtmpOutput::SendPacket(const EncodedPacket& pkt)
{
    // بعد الاتصال أو بعد رمي القطع: منستنى إطار كامل
    if (m_waitKeyframe)
    {
        if (!pkt.video || !pkt.keyframe)
            return true;
        m_waitKeyframe = false;
    }

    // الوقت بيبلّش من صفر بكل اتصال
    if (!m_haveBase)
    {
        m_haveBase = true;
        m_baseMs = pkt.timeMs;
    }
    if (pkt.timeMs < m_baseMs)
        return true;                    // صوت أقدم من أول إطار
    const uint32_t ts = pkt.timeMs - m_baseMs;

    if (pkt.video)
    {
        if (!m_videoHeaderSent)
        {
            const std::vector<uint8_t> cfg = m_getConfig ? m_getConfig() : std::vector<uint8_t>();
            if (cfg.empty())
            {
                m_waitKeyframe = true;  // لسا ما عنا SPS/PPS
                m_haveBase = false;
                return true;
            }
            const std::vector<uint8_t> h = bs::FlvVideoHeader(m_info.hevc, cfg);
            if (!SendRtmp(CS_VIDEO, MSG_VIDEO, ts, m_streamId, h.data(), h.size()))
                return false;
            m_videoHeaderSent = true;
        }
        // H.264 بالطريقة القديمة ، أو HEVC بـ Enhanced RTMP. ctsMs = فرق B-frames
        std::vector<uint8_t> v;
        bs::FlvVideoFrame(m_info.hevc, pkt.keyframe, pkt.ctsMs, pkt.data.data(), pkt.data.size(), v);
        return SendRtmp(CS_VIDEO, MSG_VIDEO, ts, m_streamId, v.data(), v.size());
    }

    std::vector<uint8_t> a;
    a.reserve(pkt.data.size() + 2);
    a.push_back(0xAF);                  // AAC ، 44kHz+ ، 16 بت ، ستيريو
    a.push_back(0x01);                  // بيانات خام
    a.insert(a.end(), pkt.data.begin(), pkt.data.end());
    return SendRtmp(CS_AUDIO, MSG_AUDIO, ts, m_streamId, a.data(), a.size());
}

// -------------------------------------------------------------
//  المصافحة: C0+C1 -> S0+S1+S2 -> C2
// -------------------------------------------------------------
bool RtmpOutput::Handshake()
{
    std::vector<uint8_t> c(1 + 1536);
    c[0] = 3;
    const uint32_t now = GetTickCount();
    c[1] = uint8_t(now >> 24); c[2] = uint8_t(now >> 16); c[3] = uint8_t(now >> 8); c[4] = uint8_t(now);
    uint32_t seed = now * 2654435761u;
    for (size_t i = 9; i < c.size(); ++i)
    {
        seed = seed * 1103515245u + 12345u;
        c[i] = uint8_t(seed >> 16);
    }
    if (!m_t->SendAll(c.data(), c.size()))
        return false;

    // S0 + S1
    std::vector<uint8_t> s(1 + 1536 + 1536);
    size_t got = 0;
    const double deadline = NowSeconds() + 10;
    auto readTo = [&](size_t want) {
        while (got < want)
        {
            if (NowSeconds() > deadline) return false;
            const int r = m_t->Recv(s.data() + got, want - got, 200);
            if (r < 0) return false;
            got += r;
        }
        return true;
    };
    if (!readTo(1 + 1536))
        return false;

    // C2 = نسخة من S1
    if (!m_t->SendAll(s.data() + 1, 1536))
        return false;

    // S2
    return readTo(s.size());
}

bool RtmpOutput::SendChunkSize(uint32_t size)
{
    const uint8_t d[4] = { uint8_t(size >> 24), uint8_t(size >> 16), uint8_t(size >> 8), uint8_t(size) };
    if (!SendRtmp(CS_CONTROL, MSG_CHUNK_SIZE, 0, 0, d, 4))
        return false;
    m_outChunk = size;
    return true;
}

bool RtmpOutput::SendCommand(uint8_t csid, uint32_t streamId, const std::vector<uint8_t>& amf)
{
    return SendRtmp(csid, MSG_CMD, 0, streamId, amf.data(), amf.size());
}

// الرسالة بتنقسم لقطع (chunks)، كل قطعة إلها رأس صغير
bool RtmpOutput::SendRtmp(uint8_t csid, uint8_t type, uint32_t ts, uint32_t streamId, const uint8_t* data, size_t len)
{
    const bool extended = ts >= 0xFFFFFF;
    const uint32_t ts24 = extended ? 0xFFFFFF : ts;

    std::vector<uint8_t> out;
    out.reserve(len + 16 + (len / m_outChunk + 1) * 5);

    // الرأس الكامل (fmt 0)
    out.push_back(csid & 0x3F);
    out.push_back(uint8_t(ts24 >> 16)); out.push_back(uint8_t(ts24 >> 8)); out.push_back(uint8_t(ts24));
    out.push_back(uint8_t(len >> 16));  out.push_back(uint8_t(len >> 8));  out.push_back(uint8_t(len));
    out.push_back(type);
    out.push_back(uint8_t(streamId)); out.push_back(uint8_t(streamId >> 8));
    out.push_back(uint8_t(streamId >> 16)); out.push_back(uint8_t(streamId >> 24));   // little-endian
    auto pushExt = [&] {
        if (extended)
        {
            out.push_back(uint8_t(ts >> 24)); out.push_back(uint8_t(ts >> 16));
            out.push_back(uint8_t(ts >> 8));  out.push_back(uint8_t(ts));
        }
    };
    pushExt();

    size_t off = 0;
    while (true)
    {
        const size_t n = std::min<size_t>(m_outChunk, len - off);
        out.insert(out.end(), data + off, data + off + n);
        off += n;
        if (off >= len)
            break;
        out.push_back(0xC0 | (csid & 0x3F));   // fmt 3: كمّل نفس الرسالة
        pushExt();
    }

    if (!m_t->SendAll(out.data(), out.size()))
        return false;
    m_bytesSent += out.size();
    return true;
}

// -------------------------------------------------------------
//  القراءة من السيرفر
// -------------------------------------------------------------

// بيحاول يقرا قطعة وحدة من m_rbuf بدون ما ياخد شي إذا البيانات ناقصة
int RtmpOutput::ParseChunk(Message& msg, bool& complete)
{
    complete = false;
    const uint8_t* b = m_rbuf.data();
    const size_t   size = m_rbuf.size();
    size_t p = 0;

    if (size < 1) return 0;
    const uint8_t fmt = b[0] >> 6;
    uint32_t csid = b[0] & 0x3F;
    p = 1;
    if (csid == 0)      { if (size < 2) return 0; csid = 64 + b[1]; p = 2; }
    else if (csid == 1) { if (size < 3) return 0; csid = 64 + b[1] + b[2] * 256; p = 3; }

    static const size_t headerLen[4] = { 11, 7, 3, 0 };
    if (size < p + headerLen[fmt]) return 0;

    ChunkState& st = m_in[csid];
    ChunkState next = st;
    uint32_t ts = 0;
    if (fmt <= 2)
    {
        ts = uint32_t(b[p]) << 16 | uint32_t(b[p + 1]) << 8 | b[p + 2];
        next.extended = ts == 0xFFFFFF;
    }
    if (fmt <= 1)
    {
        next.len  = uint32_t(b[p + 3]) << 16 | uint32_t(b[p + 4]) << 8 | b[p + 5];
        next.type = b[p + 6];
    }
    if (fmt == 0)
        next.streamId = uint32_t(b[p + 7]) | uint32_t(b[p + 8]) << 8 | uint32_t(b[p + 9]) << 16 | uint32_t(b[p + 10]) << 24;
    p += headerLen[fmt];

    if (next.extended)
    {
        if (size < p + 4) return 0;
        p += 4;
    }

    if (fmt != 3)
        next.payload.clear();           // رسالة جديدة
    if (next.len > 16 * 1024 * 1024)
        return -1;

    const size_t remaining = next.len - next.payload.size();
    const size_t n = std::min<size_t>(remaining, m_inChunk);
    if (size < p + n) return 0;

    next.payload.insert(next.payload.end(), b + p, b + p + n);
    p += n;

    m_rbuf.erase(m_rbuf.begin(), m_rbuf.begin() + p);

    if (next.payload.size() >= next.len)
    {
        msg.type     = next.type;
        msg.streamId = next.streamId;
        msg.data     = std::move(next.payload);
        next.payload.clear();
        complete = true;
    }
    st = std::move(next);
    return 1;
}

// بيقرا رسالة كاملة. false = انقطع أو خلصت المهلة (msg.type = 0)
bool RtmpOutput::ReadMessage(Message& msg, int timeoutMs)
{
    msg = Message{};
    const double deadline = NowSeconds() + timeoutMs / 1000.0;
    while (true)
    {
        bool complete = false;
        const int r = ParseChunk(msg, complete);
        if (r < 0) return false;
        if (r > 0)
        {
            if (complete) return true;
            continue;
        }

        // بدنا بيانات زيادة
        const double left = deadline - NowSeconds();
        if (left <= 0)
            return true;                // ما في شي (msg.type = 0)
        uint8_t tmp[16384];
        const int got = m_t->Recv(tmp, sizeof(tmp), std::max(0, (int)(left * 1000)));
        if (got < 0)
            return false;
        if (got == 0)
        {
            if (timeoutMs == 0) return true;
            continue;
        }
        m_rbuf.insert(m_rbuf.end(), tmp, tmp + got);
        m_received += got;
        if (!SendAckIfNeeded())
            return false;
    }
}

bool RtmpOutput::SendAckIfNeeded()
{
    if (m_window == 0 || m_received - m_lastAck < m_window / 2)
        return true;
    m_lastAck = m_received;
    const uint32_t v = (uint32_t)m_received;
    const uint8_t d[4] = { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
    return SendRtmp(CS_CONTROL, MSG_ACK, 0, 0, d, 4);
}

// رسائل التحكم. بيرجّع true إذا كانت رسالة تحكم (وعالجناها)
bool RtmpOutput::HandleControl(const Message& msg)
{
    const auto& d = msg.data;
    auto u32 = [&](size_t i) { return uint32_t(d[i]) << 24 | uint32_t(d[i + 1]) << 16 | uint32_t(d[i + 2]) << 8 | d[i + 3]; };

    switch (msg.type)
    {
    case MSG_CHUNK_SIZE:
        if (d.size() >= 4) m_inChunk = std::max<uint32_t>(1, u32(0) & 0x7FFFFFFF);
        return true;
    case MSG_WINDOW:
        if (d.size() >= 4) m_window = u32(0);
        return true;
    case MSG_PEER_BW:
    {
        // منرد بنفس حجم النافذة
        if (d.size() >= 4)
        {
            const uint32_t w = u32(0);
            const uint8_t r[4] = { uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w) };
            SendRtmp(CS_CONTROL, MSG_WINDOW, 0, 0, r, 4);
        }
        return true;
    }
    case MSG_USER:
        // ping (6) -> pong (7) بنفس الوقت
        if (d.size() >= 6 && d[0] == 0 && d[1] == 6)
        {
            const uint8_t r[6] = { 0, 7, d[2], d[3], d[4], d[5] };
            SendRtmp(CS_CONTROL, MSG_USER, 0, 0, r, 6);
        }
        return true;
    case MSG_ACK:
    case MSG_ABORT:
        return true;
    default:
        return false;
    }
}

// وقت البث: منقرا أي شي بعته السيرفر بدون ما نستنى
bool RtmpOutput::PollIncoming()
{
    for (int i = 0; i < 16; ++i)
    {
        Message msg;
        if (!ReadMessage(msg, 0))
            return false;
        if (msg.type == 0)
            return true;
        HandleControl(msg);
    }
    return true;
}

// بنستنى رد على أمر (_result) رقمه txn
bool RtmpOutput::WaitForResult(double txn, double* number, std::wstring& error, int timeoutMs)
{
    const double deadline = NowSeconds() + timeoutMs / 1000.0;
    while (m_running && NowSeconds() < deadline)
    {
        Message msg;
        if (!ReadMessage(msg, 200))
        {
            error = L"انقطع الاتصال بالخادم";
            return false;
        }
        if (msg.type == 0 || HandleControl(msg))
            continue;
        if (msg.type != MSG_CMD && msg.type != MSG_CMD_AMF3)
            continue;

        const size_t skip = msg.type == MSG_CMD_AMF3 && !msg.data.empty() ? 1 : 0;
        const auto vals = DecodeAll(msg.data.data() + skip, msg.data.size() - skip);
        if (vals.size() < 2 || vals[0].type != 0x02 || vals[1].num != txn)
            continue;

        if (vals[0].str == "_result")
        {
            if (number)
                for (size_t i = 2; i < vals.size(); ++i)
                    if (vals[i].type == 0x00) { *number = vals[i].num; break; }
            return true;
        }
        if (vals[0].str == "_error")
        {
            std::string desc;
            for (size_t i = 2; i < vals.size(); ++i)
                if (vals[i].type == 0x03) desc = vals[i].GetStr("description");
            error = L"رفض الخادم: " + Wide(desc);
            m_fatal = true;
            return false;
        }
    }
    if (error.empty())
        error = L"لم يردّ الخادم";
    return false;
}

// بنستنى "NetStream.Publish.Start" = المنصة قبلت المفتاح
bool RtmpOutput::WaitForPublish(std::wstring& error, int timeoutMs)
{
    const double deadline = NowSeconds() + timeoutMs / 1000.0;
    while (m_running && NowSeconds() < deadline)
    {
        Message msg;
        if (!ReadMessage(msg, 200))
        {
            error = L"أغلق الخادم الاتصال (تأكّد من مفتاح البث)";
            m_fatal = true;
            return false;
        }
        if (msg.type == 0 || HandleControl(msg))
            continue;
        if (msg.type != MSG_CMD && msg.type != MSG_CMD_AMF3)
            continue;

        const size_t skip = msg.type == MSG_CMD_AMF3 && !msg.data.empty() ? 1 : 0;
        const auto vals = DecodeAll(msg.data.data() + skip, msg.data.size() - skip);
        if (vals.empty() || vals[0].type != 0x02)
            continue;

        if (vals[0].str == "onStatus")
        {
            for (size_t i = 2; i < vals.size(); ++i)
            {
                if (vals[i].type != 0x03) continue;
                const std::string code  = vals[i].GetStr("code");
                const std::string level = vals[i].GetStr("level");
                if (code == "NetStream.Publish.Start")
                    return true;
                if (level == "error" || code.find("Failed") != std::string::npos ||
                    code.find("BadName") != std::string::npos || code.find("Rejected") != std::string::npos)
                {
                    const std::string desc = vals[i].GetStr("description");
                    error = L"رفضت المنصة البث: " + Wide(desc.empty() ? code : desc);
                    m_fatal = true;
                    return false;
                }
            }
        }
        else if (vals[0].str == "_error" && vals.size() >= 2 && vals[1].num == 5)
        {
            error = L"رفضت المنصة مفتاح البث";
            m_fatal = true;
            return false;
        }
    }
    if (error.empty())
        error = L"لم تؤكّد المنصة بدء البث";
    return false;
}
