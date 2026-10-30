// =============================================================
//  Bitstream.cpp - شوف Bitstream.h
// =============================================================
#include "Bitstream.h"

namespace bs
{
    namespace
    {
        void Put16(std::vector<uint8_t>& v, size_t x) { v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); }
        void Put24(std::vector<uint8_t>& v, int32_t x)
        {
            v.push_back(uint8_t((x >> 16) & 0xFF)); v.push_back(uint8_t((x >> 8) & 0xFF)); v.push_back(uint8_t(x & 0xFF));
        }

        int  NalType(const uint8_t* nal, bool hevc) { return hevc ? (nal[0] >> 1) & 0x3F : nal[0] & 0x1F; }

        // بيشيل بايتات الحماية (00 00 03 → 00 00) لحتى نقرا القيم
        std::vector<uint8_t> Unescape(const uint8_t* p, size_t n)
        {
            std::vector<uint8_t> out;
            out.reserve(n);
            int zeros = 0;
            for (size_t i = 0; i < n; ++i)
            {
                if (zeros >= 2 && p[i] == 3)
                {
                    zeros = 0;
                    continue;
                }
                zeros = p[i] == 0 ? zeros + 1 : 0;
                out.push_back(p[i]);
            }
            return out;
        }

        // HEVC: أنواع الوحدات
        constexpr int HEVC_VPS = 32, HEVC_SPS = 33, HEVC_PPS = 34, HEVC_AUD = 35, HEVC_SEI_PREFIX = 39, HEVC_SEI_SUFFIX = 40;
        // H.264
        constexpr int AVC_SEI = 6, AVC_SPS = 7, AVC_PPS = 8, AVC_AUD = 9, AVC_IDR = 5;
    }

    std::vector<std::pair<const uint8_t*, size_t>> SplitAnnexB(const uint8_t* p, size_t n)
    {
        std::vector<size_t> starts;
        for (size_t i = 0; i + 3 <= n; )
        {
            if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1)
            {
                starts.push_back(i + 3);
                i += 3;
            }
            else
                ++i;
        }
        std::vector<std::pair<const uint8_t*, size_t>> nals;
        for (size_t k = 0; k < starts.size(); ++k)
        {
            const size_t begin = starts[k];
            size_t end = (k + 1 < starts.size()) ? starts[k + 1] - 3 : n;
            while (end > begin && p[end - 1] == 0)     // الصفر الزيادة (00 00 00 01)
                --end;
            if (end > begin)
                nals.push_back({ p + begin, end - begin });
        }
        return nals;
    }

    bool ToLengthPrefixed(const uint8_t* p, size_t n, bool hevc, std::vector<uint8_t>& out)
    {
        out.clear();
        bool key = false;
        for (const auto& [nal, size] : SplitAnnexB(p, n))
        {
            if (size < (hevc ? 2u : 1u))
                continue;
            const int t = NalType(nal, hevc);
            if (hevc)
            {
                if (t == HEVC_VPS || t == HEVC_SPS || t == HEVC_PPS || t == HEVC_AUD ||
                    t == HEVC_SEI_PREFIX || t == HEVC_SEI_SUFFIX)
                    continue;
                if (t >= 16 && t <= 23) key = true;       // IRAP (IDR / CRA / BLA)
            }
            else
            {
                if (t == AVC_SPS || t == AVC_PPS || t == AVC_AUD || t == AVC_SEI)
                    continue;
                if (t == AVC_IDR) key = true;
            }
            out.push_back(uint8_t(size >> 24));
            out.push_back(uint8_t(size >> 16));
            out.push_back(uint8_t(size >> 8));
            out.push_back(uint8_t(size));
            out.insert(out.end(), nal, nal + size);
        }
        return key;
    }

    std::vector<uint8_t> BuildAvcC(const uint8_t* p, size_t n)
    {
        const uint8_t* sps = nullptr; size_t spsN = 0;
        const uint8_t* pps = nullptr; size_t ppsN = 0;
        for (const auto& [nal, size] : SplitAnnexB(p, n))
        {
            const int t = NalType(nal, false);
            if (t == AVC_SPS && !sps) { sps = nal; spsN = size; }
            if (t == AVC_PPS && !pps) { pps = nal; ppsN = size; }
        }
        if (!sps || spsN < 4 || !pps)
            return {};
        std::vector<uint8_t> c = { 1, sps[1], sps[2], sps[3], 0xFF, 0xE1 };
        Put16(c, spsN);
        c.insert(c.end(), sps, sps + spsN);
        c.push_back(1);
        Put16(c, ppsN);
        c.insert(c.end(), pps, pps + ppsN);
        return c;
    }

    std::vector<uint8_t> BuildHvcC(const uint8_t* p, size_t n)
    {
        std::vector<std::pair<const uint8_t*, size_t>> vps, sps, pps;
        for (const auto& nal : SplitAnnexB(p, n))
        {
            if (nal.second < 2) continue;
            const int t = NalType(nal.first, true);
            if (t == HEVC_VPS) vps.push_back(nal);
            if (t == HEVC_SPS) sps.push_back(nal);
            if (t == HEVC_PPS) pps.push_back(nal);
        }
        if (vps.empty() || sps.empty() || pps.empty())
            return {};

        // من الـ SPS: أول 13 بايت بعد رأس الوحدة (بايتين) فيهم البروفايل والمستوى كاملين
        const std::vector<uint8_t> r = Unescape(sps[0].first + 2, sps[0].second - 2);
        if (r.size() < 13)
            return {};
        const int maxSubLayersMinus1 = (r[0] >> 1) & 7;
        const int temporalIdNesting  = r[0] & 1;

        std::vector<uint8_t> c;
        c.push_back(1);                                   // configurationVersion
        c.insert(c.end(), r.begin() + 1, r.begin() + 13); // profile/tier/idc + compat(4) + constraints(6) + level
        c.push_back(0xF0); c.push_back(0x00);             // min_spatial_segmentation_idc = 0
        c.push_back(0xFC);                                // parallelismType = 0
        c.push_back(0xFC | 1);                            // chroma_format_idc = 1 (4:2:0)
        c.push_back(0xF8 | 0);                            // bit_depth_luma - 8
        c.push_back(0xF8 | 0);                            // bit_depth_chroma - 8
        c.push_back(0); c.push_back(0);                   // avgFrameRate
        c.push_back(uint8_t(((maxSubLayersMinus1 + 1) << 3) | (temporalIdNesting << 2) | 3));   // lengthSize = 4
        c.push_back(3);                                   // 3 مجموعات: VPS ، SPS ، PPS
        for (auto* group : { &vps, &sps, &pps })
        {
            c.push_back(uint8_t(0x80 | NalType((*group)[0].first, true)));   // array_completeness = 1
            Put16(c, group->size());
            for (const auto& [nal, size] : *group)
            {
                Put16(c, size);
                c.insert(c.end(), nal, nal + size);
            }
        }
        return c;
    }

    std::vector<uint8_t> FlvVideoHeader(bool hevc, const std::vector<uint8_t>& config)
    {
        std::vector<uint8_t> v;
        if (hevc)
        {
            v = { uint8_t(0x80 | (1 << 4) | 0), 'h', 'v', 'c', '1' };    // ExHeader | key | SequenceStart
        }
        else
        {
            v = { 0x17, 0x00, 0, 0, 0 };                                  // AVC key | sequence header
        }
        v.insert(v.end(), config.begin(), config.end());
        return v;
    }

    void FlvVideoFrame(bool hevc, bool key, int32_t ctsMs, const uint8_t* data, size_t n, std::vector<uint8_t>& out)
    {
        out.clear();
        out.reserve(n + 8);
        const uint8_t frameType = key ? 1 : 2;
        if (hevc)
        {
            // CodedFrames (1) مع وقت العرض ، أو CodedFramesX (3) إذا الفرق صفر (أوفر بـ 3 بايت)
            const uint8_t packetType = ctsMs != 0 ? 1 : 3;
            out.push_back(uint8_t(0x80 | (frameType << 4) | packetType));
            out.push_back('h'); out.push_back('v'); out.push_back('c'); out.push_back('1');
            if (packetType == 1)
                Put24(out, ctsMs);
        }
        else
        {
            out.push_back(uint8_t((frameType << 4) | 7));
            out.push_back(0x01);                       // NALU
            Put24(out, ctsMs);
        }
        out.insert(out.end(), data, data + n);
    }

    void LengthPrefixedToAnnexB(const uint8_t* p, size_t n, std::vector<uint8_t>& out)
    {
        out.clear();
        out.reserve(n + 16);
        size_t i = 0;
        while (i + 4 <= n)
        {
            const size_t len = (size_t(p[i]) << 24) | (size_t(p[i + 1]) << 16) | (size_t(p[i + 2]) << 8) | p[i + 3];
            i += 4;
            if (len == 0 || i + len > n)
                break;
            out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(1);
            out.insert(out.end(), p + i, p + i + len);
            i += len;
        }
    }

    std::vector<uint8_t> ConfigToAnnexB(const std::vector<uint8_t>& c, bool hevc)
    {
        std::vector<uint8_t> out;
        auto put = [&](size_t at, size_t len)
        {
            if (at + len > c.size()) return false;
            out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(1);
            out.insert(out.end(), c.begin() + at, c.begin() + at + len);
            return true;
        };
        auto u16 = [&](size_t at) { return at + 2 <= c.size() ? (size_t(c[at]) << 8) | c[at + 1] : 0; };
        if (!hevc)
        {
            if (c.size() < 7) return {};
            size_t i = 5;
            const int sps = c[i++] & 0x1F;
            for (int k = 0; k < sps; ++k) { const size_t l = u16(i); i += 2; if (!put(i, l)) return {}; i += l; }
            if (i >= c.size()) return out;
            const int pps = c[i++];
            for (int k = 0; k < pps; ++k) { const size_t l = u16(i); i += 2; if (!put(i, l)) return {}; i += l; }
            return out;
        }
        if (c.size() < 23) return {};
        size_t i = 22;
        const int arrays = c[i++];
        for (int a = 0; a < arrays; ++a)
        {
            if (i + 3 > c.size()) return {};
            ++i;                                   // النوع
            const size_t count = u16(i); i += 2;
            for (size_t k = 0; k < count; ++k) { const size_t l = u16(i); i += 2; if (!put(i, l)) return {}; i += l; }
        }
        return out;
    }

    double FlvVideoCodecId(bool hevc)
    {
        return hevc ? (double)(('h' << 24) | ('v' << 16) | ('c' << 8) | '1') : 7.0;
    }
}
