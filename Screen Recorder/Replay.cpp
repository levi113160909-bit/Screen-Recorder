// =============================================================
//  Replay.cpp
// =============================================================
#include "Replay.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// -------------------------------------------------------------
//  ReplayBuffer
// -------------------------------------------------------------
void ReplayBuffer::SetSeconds(int seconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_seconds = std::clamp(seconds, 5, 600);
}

void ReplayBuffer::Push(const std::shared_ptr<const EncodedPacket>& pkt)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_packets.push_back(pkt);
    // منخلّي زيادة 3 ثواني لحتى نلاقي إطار كامل نبلّش منه
    const uint32_t keep = (uint32_t)m_seconds * 1000 + 3000;
    while (m_packets.size() > 2)
    {
        uint32_t newest = 0;
        for (auto it = m_packets.rbegin(); it != m_packets.rend(); ++it)
            if ((*it)->video) { newest = (*it)->timeMs; break; }
        if (newest == 0 || newest - m_packets.front()->timeMs <= keep || m_packets.front()->timeMs > newest)
            break;
        m_packets.pop_front();
    }
}

void ReplayBuffer::SetVideoInfo(const std::vector<uint8_t>& avcConfig, int width, int height)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_avcConfig = avcConfig;
    m_width  = width;
    m_height = height;
}

void ReplayBuffer::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_packets.clear();
}

double ReplayBuffer::BufferedSeconds()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_packets.size() < 2)
        return 0;
    const double s = (m_packets.back()->timeMs - m_packets.front()->timeMs) / 1000.0;
    return std::min<double>(s, m_seconds);
}

bool ReplayBuffer::Save(const std::wstring& path, std::wstring& error)
{
    std::vector<std::shared_ptr<const EncodedPacket>> all;
    std::vector<uint8_t> config;
    int w, h, seconds;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        all.assign(m_packets.begin(), m_packets.end());
        config  = m_avcConfig;
        w       = m_width;
        h       = m_height;
        seconds = m_seconds;
    }
    if (config.empty() || all.empty())
    {
        error = L"لا يوجد شيء محفوظ بعد";
        return false;
    }

    // آخر وقت فيديو
    uint32_t last = 0;
    for (auto it = all.rbegin(); it != all.rend(); ++it)
        if ((*it)->video) { last = (*it)->timeMs; break; }
    const uint32_t wantStart = last > (uint32_t)seconds * 1000 ? last - (uint32_t)seconds * 1000 : 0;

    // منبلّش من آخر إطار كامل قبل البداية المطلوبة (أو أول واحد بعدها)
    int startIdx = -1;
    for (int i = 0; i < (int)all.size(); ++i)
    {
        const auto& p = all[i];
        if (!p->video || !p->keyframe)
            continue;
        if (p->timeMs <= wantStart || startIdx < 0)
            startIdx = i;
        if (p->timeMs > wantStart)
            break;
    }
    if (startIdx < 0)
    {
        error = L"لا يوجد شيء محفوظ بعد";
        return false;
    }
    const uint32_t base = all[startIdx]->timeMs;
    std::vector<std::shared_ptr<const EncodedPacket>> pick;
    for (const auto& p : all)
        if (p->timeMs >= base)
            pick.push_back(p);

    if (!WriteMp4(path, pick, config, w, h))
    {
        error = L"تعذّر كتابة الملف";
        return false;
    }
    return true;
}

// =============================================================
//  كاتب MP4
//  الملف عبارة عن "صناديق" (boxes) جوّا بعض:
//    ftyp (نوع الملف) + mdat (البيانات) + moov (الفهرس)
// =============================================================
namespace
{
    struct Buf
    {
        std::vector<uint8_t> b;
        void U8(uint32_t v)  { b.push_back((uint8_t)v); }
        void U16(uint32_t v) { U8(v >> 8); U8(v); }
        void U24(uint32_t v) { U8(v >> 16); U8(v >> 8); U8(v); }
        void U32(uint32_t v) { U8(v >> 24); U8(v >> 16); U8(v >> 8); U8(v); }
        void Tag(const char* t) { b.insert(b.end(), t, t + 4); }
        void Zeros(size_t n) { b.insert(b.end(), n, 0); }
        void Bytes(const std::vector<uint8_t>& v) { b.insert(b.end(), v.begin(), v.end()); }
        size_t Begin(const char* type) { const size_t at = b.size(); U32(0); Tag(type); return at; }
        void End(size_t at)
        {
            const uint32_t size = (uint32_t)(b.size() - at);
            b[at] = (uint8_t)(size >> 24); b[at + 1] = (uint8_t)(size >> 16);
            b[at + 2] = (uint8_t)(size >> 8); b[at + 3] = (uint8_t)size;
        }
        size_t FullBox(const char* type, uint32_t version, uint32_t flags)
        {
            const size_t at = Begin(type);
            U8(version);
            U24(flags);
            return at;
        }
        void Matrix()
        {
            const uint32_t m[9] = { 0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000 };
            for (uint32_t v : m) U32(v);
        }
    };

    struct Track
    {
        bool                  video = true;
        uint32_t              timescale = 1000;
        std::vector<uint32_t> sizes, durations, offsets, keyframes;   // keyframes: أرقام تبدأ من 1
        uint64_t Duration() const { uint64_t d = 0; for (uint32_t x : durations) d += x; return d; }
    };

    void WriteStbl(Buf& o, const Track& t, const std::vector<uint8_t>& avcConfig, int width, int height)
    {
        const size_t stbl = o.Begin("stbl");

        // stsd: وصف الترميز
        {
            const size_t stsd = o.FullBox("stsd", 0, 0);
            o.U32(1);
            if (t.video)
            {
                const size_t avc1 = o.Begin("avc1");
                o.Zeros(6); o.U16(1);                 // data reference index
                o.Zeros(16);
                o.U16(width); o.U16(height);
                o.U32(0x00480000); o.U32(0x00480000); // 72 dpi
                o.U32(0); o.U16(1);                   // frame count
                o.Zeros(32);                          // compressor name
                o.U16(0x0018); o.U16(0xFFFF);
                const size_t avcC = o.Begin("avcC");
                o.Bytes(avcConfig);
                o.End(avcC);
                o.End(avc1);
            }
            else
            {
                const size_t mp4a = o.Begin("mp4a");
                o.Zeros(6); o.U16(1);
                o.Zeros(8);
                o.U16(2); o.U16(16);                  // ستيريو ، 16 بت
                o.U16(0); o.U16(0);
                o.U32(48000u << 16);
                const size_t esds = o.FullBox("esds", 0, 0);
                const std::vector<uint8_t> asc = AacEncoder::AudioConfig();
                o.U8(0x03); o.U8(23 + (uint32_t)asc.size()); o.U16(0); o.U8(0);              // ES_Descriptor
                o.U8(0x04); o.U8(15 + (uint32_t)asc.size());                                  // DecoderConfig
                o.U8(0x40); o.U8(0x15); o.U24(0); o.U32(160000); o.U32(160000);
                o.U8(0x05); o.U8((uint32_t)asc.size()); o.Bytes(asc);                         // AudioSpecificConfig
                o.U8(0x06); o.U8(1); o.U8(0x02);                                               // SLConfig
                o.End(esds);
                o.End(mp4a);
            }
            o.End(stsd);
        }

        // stts: مدة كل عينة (مضغوطة: كم وحدة ورا بعض بنفس المدة)
        {
            std::vector<std::pair<uint32_t, uint32_t>> runs;
            for (uint32_t d : t.durations)
            {
                if (!runs.empty() && runs.back().second == d) runs.back().first++;
                else runs.push_back({ 1, d });
            }
            const size_t stts = o.FullBox("stts", 0, 0);
            o.U32((uint32_t)runs.size());
            for (auto& r : runs) { o.U32(r.first); o.U32(r.second); }
            o.End(stts);
        }

        // stss: الإطارات الكاملة (للفيديو بس)
        if (t.video)
        {
            const size_t stss = o.FullBox("stss", 0, 0);
            o.U32((uint32_t)t.keyframes.size());
            for (uint32_t k : t.keyframes) o.U32(k);
            o.End(stss);
        }

        // stsc: كل عينة بـ "chunk" لحالها
        {
            const size_t stsc = o.FullBox("stsc", 0, 0);
            o.U32(1); o.U32(1); o.U32(1); o.U32(1);
            o.End(stsc);
        }

        // stsz: حجم كل عينة
        {
            const size_t stsz = o.FullBox("stsz", 0, 0);
            o.U32(0);
            o.U32((uint32_t)t.sizes.size());
            for (uint32_t s : t.sizes) o.U32(s);
            o.End(stsz);
        }

        // stco: وين كل عينة بالملف
        {
            const size_t stco = o.FullBox("stco", 0, 0);
            o.U32((uint32_t)t.offsets.size());
            for (uint32_t off : t.offsets) o.U32(off);
            o.End(stco);
        }
        o.End(stbl);
    }

    void WriteTrak(Buf& o, const Track& t, uint32_t id, const std::vector<uint8_t>& avcConfig, int width, int height)
    {
        const uint64_t mediaDur = t.Duration();
        const uint32_t movieDur = (uint32_t)(mediaDur * 1000 / t.timescale);

        const size_t trak = o.Begin("trak");
        {
            const size_t tkhd = o.FullBox("tkhd", 0, 3);
            o.U32(0); o.U32(0); o.U32(id); o.U32(0); o.U32(movieDur);
            o.Zeros(8); o.U16(0); o.U16(0);
            o.U16(t.video ? 0 : 0x0100); o.U16(0);
            o.Matrix();
            o.U32(t.video ? (uint32_t)width << 16 : 0);
            o.U32(t.video ? (uint32_t)height << 16 : 0);
            o.End(tkhd);
        }
        const size_t mdia = o.Begin("mdia");
        {
            const size_t mdhd = o.FullBox("mdhd", 0, 0);
            o.U32(0); o.U32(0); o.U32(t.timescale); o.U32((uint32_t)mediaDur);
            o.U16(0x55C4); o.U16(0);          // اللغة: und
            o.End(mdhd);

            const size_t hdlr = o.FullBox("hdlr", 0, 0);
            o.U32(0); o.Tag(t.video ? "vide" : "soun"); o.Zeros(12);
            const char* name = t.video ? "VideoHandler" : "SoundHandler";
            o.b.insert(o.b.end(), name, name + strlen(name) + 1);
            o.End(hdlr);

            const size_t minf = o.Begin("minf");
            if (t.video)
            {
                const size_t vmhd = o.FullBox("vmhd", 0, 1);
                o.Zeros(8);
                o.End(vmhd);
            }
            else
            {
                const size_t smhd = o.FullBox("smhd", 0, 0);
                o.Zeros(4);
                o.End(smhd);
            }
            const size_t dinf = o.Begin("dinf");
            const size_t dref = o.FullBox("dref", 0, 0);
            o.U32(1);
            const size_t url = o.FullBox("url ", 0, 1);
            o.End(url);
            o.End(dref);
            o.End(dinf);
            WriteStbl(o, t, avcConfig, width, height);
            o.End(minf);
        }
        o.End(mdia);
        o.End(trak);
    }
}

bool WriteMp4(const std::wstring& path, const std::vector<std::shared_ptr<const EncodedPacket>>& packets,
              const std::vector<uint8_t>& avcConfig, int width, int height)
{
    if (packets.empty())
        return false;
    const uint32_t base = packets.front()->timeMs;

    // ftyp
    Buf head;
    {
        const size_t ftyp = head.Begin("ftyp");
        head.Tag("isom"); head.U32(512);
        head.Tag("isom"); head.Tag("iso2"); head.Tag("avc1"); head.Tag("mp41");
        head.End(ftyp);
    }

    // mdat: منحسب الحجم أول
    uint64_t dataSize = 0;
    for (const auto& p : packets) dataSize += p->data.size();
    if (dataSize + 16 > 0xFFFFFFF0ull)
        return false;                        // أكبر من 4 جيجا (ما رح يصير بالإعادة)

    Track video, audio;
    video.video = true;  video.timescale = 1000;
    audio.video = false; audio.timescale = 48000;

    uint32_t offset = (uint32_t)head.b.size() + 8;
    std::vector<uint32_t> videoTimes;
    for (const auto& p : packets)
    {
        if (p->video)
        {
            video.sizes.push_back((uint32_t)p->data.size());
            video.offsets.push_back(offset);
            videoTimes.push_back(p->timeMs - base);
            if (p->keyframe)
                video.keyframes.push_back((uint32_t)video.sizes.size());
        }
        else
        {
            audio.sizes.push_back((uint32_t)p->data.size());
            audio.offsets.push_back(offset);
            audio.durations.push_back(1024);
        }
        offset += (uint32_t)p->data.size();
    }
    if (video.sizes.empty())
        return false;

    // مدة كل إطار = الفرق للإطار اللي بعده
    for (size_t i = 0; i < videoTimes.size(); ++i)
    {
        uint32_t d = 33;
        if (i + 1 < videoTimes.size())
            d = std::max<uint32_t>(1, videoTimes[i + 1] - videoTimes[i]);
        else if (i > 0)
            d = std::max<uint32_t>(1, videoTimes[i] - videoTimes[i - 1]);
        video.durations.push_back(d);
    }

    // moov
    Buf moov;
    {
        const size_t at = moov.Begin("moov");
        const uint32_t dur = (uint32_t)std::max<uint64_t>(video.Duration(), audio.Duration() * 1000 / 48000);
        const size_t mvhd = moov.FullBox("mvhd", 0, 0);
        moov.U32(0); moov.U32(0); moov.U32(1000); moov.U32(dur);
        moov.U32(0x00010000); moov.U16(0x0100); moov.Zeros(10);
        moov.Matrix();
        moov.Zeros(24);
        moov.U32(audio.sizes.empty() ? 2 : 3);
        moov.End(mvhd);
        WriteTrak(moov, video, 1, avcConfig, width, height);
        if (!audio.sizes.empty())
            WriteTrak(moov, audio, 2, avcConfig, width, height);
        moov.End(at);
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
        return false;
    bool ok = fwrite(head.b.data(), 1, head.b.size(), f) == head.b.size();
    Buf mdatHead;
    mdatHead.U32((uint32_t)(dataSize + 8));
    mdatHead.Tag("mdat");
    ok = ok && fwrite(mdatHead.b.data(), 1, 8, f) == 8;
    for (const auto& p : packets)
        ok = ok && fwrite(p->data.data(), 1, p->data.size(), f) == p->data.size();
    ok = ok && fwrite(moov.b.data(), 1, moov.b.size(), f) == moov.b.size();
    fclose(f);
    if (!ok)
        DeleteFileW(path.c_str());
    return ok;
}
