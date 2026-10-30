// =============================================================
//  Chroma.cpp - مفتاح الكروما (شوف Chroma.h)
// =============================================================
#include "Chroma.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

namespace
{
    // لون البكسل بدون سطوع (Cb, Cr) - طريقة الفيديو المعروفة
    inline int Cb(int r, int g, int b) { return 128 + ((-43 * r - 85 * g + 128 * b) >> 8); }
    inline int Cr(int r, int g, int b) { return 128 + ((128 * r - 107 * g - 21 * b) >> 8); }

    struct Entry
    {
        uint64_t                           srcVersion = 0;
        uint64_t                           sig = 0;
        std::shared_ptr<const MediaFrame>  out;
        std::vector<uint8_t>               alphaLut, spillLut;   // 256×256 حسب (Cb, Cr)
        uint64_t                           lutSig = 0;
    };
    std::mutex                    g_mutex;
    std::map<std::wstring, Entry> g_cache;          // لكل كاميرا
    std::atomic<uint64_t>         g_version{ 1u << 30 };

    uint64_t Signature(const SceneLayer& l)
    {
        uint64_t s = l.chromaColor;
        s = s * 1315423911u + (uint64_t)std::lround(l.chromaSim * 1000);
        s = s * 1315423911u + (uint64_t)std::lround(l.chromaSmooth * 1000);
        s = s * 1315423911u + (uint64_t)std::lround(l.chromaSpill * 1000);
        return s;
    }

    void BuildLuts(Entry& e, const SceneLayer& l)
    {
        const int kr = (l.chromaColor >> 16) & 255, kg = (l.chromaColor >> 8) & 255, kb = l.chromaColor & 255;
        const float kcb = (float)Cb(kr, kg, kb), kcr = (float)Cr(kr, kg, kb);
        const float sim = std::clamp(l.chromaSim, 0.01f, 1.0f), smooth = std::max(0.005f, l.chromaSmooth);
        const float spillRange = sim + smooth + 0.25f;
        e.alphaLut.resize(256 * 256);
        e.spillLut.resize(256 * 256);
        for (int cb = 0; cb < 256; ++cb)
            for (int cr = 0; cr < 256; ++cr)
            {
                const float d = std::sqrt((cb - kcb) * (cb - kcb) + (cr - kcr) * (cr - kcr)) / 128.0f;
                const float a = std::clamp((d - sim) / smooth, 0.0f, 1.0f);
                // الانعكاس: البكسل الباقي بس لونه مايل للمفتاح
                const float sp = std::clamp(1.0f - (d - sim) / (spillRange - sim), 0.0f, 1.0f) * l.chromaSpill;
                e.alphaLut[cb * 256 + cr] = (uint8_t)std::lround(a * 255);
                e.spillLut[cb * 256 + cr] = (uint8_t)std::lround(sp * 255);
            }
        e.lutSig = Signature(l);
    }
}

std::shared_ptr<const MediaFrame> ApplyChroma(const SceneLayer& l, const std::shared_ptr<const MediaFrame>& src)
{
    if (!l.chroma || !src || src->width <= 0 || src->height <= 0)
        return src;
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry& e = g_cache[l.source];
    const uint64_t sig = Signature(l);
    if (e.out && e.srcVersion == src->version && e.sig == sig)
        return e.out;                                // معمولة من قبل لهالصورة
    if (e.lutSig != sig || e.alphaLut.empty())
        BuildLuts(e, l);

    // أي قناة هي "لون المفتاح"؟ (الأخضر عادةً) لحتى نخفف انعكاسها
    const int kr = (l.chromaColor >> 16) & 255, kg = (l.chromaColor >> 8) & 255, kb = l.chromaColor & 255;
    const int keyCh = (kg >= kr && kg >= kb) ? 1 : (kb >= kr ? 0 : 2);     // BGRA: 0=B 1=G 2=R

    auto out = std::make_shared<MediaFrame>();
    out->width = src->width;
    out->height = src->height;
    out->pixels.resize(src->pixels.size());
    const uint8_t* s = src->pixels.data();
    uint8_t* d = out->pixels.data();
    const size_t n = (size_t)src->width * src->height;
    const uint8_t* alut = e.alphaLut.data();
    const uint8_t* slut = e.spillLut.data();
    for (size_t i = 0; i < n; ++i, s += 4, d += 4)
    {
        int b = s[0], g = s[1], r = s[2];
        const int cb = std::clamp(Cb(r, g, b), 0, 255), cr = std::clamp(Cr(r, g, b), 0, 255);
        const int idx = cb * 256 + cr;
        const int a = alut[idx];
        if (a == 0)
        {
            d[0] = d[1] = d[2] = d[3] = 0;
            continue;
        }
        const int sp = slut[idx];
        if (sp)
        {
            int c[3] = { b, g, r };
            const int o1 = c[(keyCh + 1) % 3], o2 = c[(keyCh + 2) % 3];
            const int limit = std::max(o1, o2);
            if (c[keyCh] > limit)
                c[keyCh] -= (c[keyCh] - limit) * sp / 255;
            b = c[0]; g = c[1]; r = c[2];
        }
        // ألوان "premultiplied" (متل ما بيحب Direct2D)
        d[0] = (uint8_t)(b * a / 255);
        d[1] = (uint8_t)(g * a / 255);
        d[2] = (uint8_t)(r * a / 255);
        d[3] = (uint8_t)a;
    }
    out->version = ++g_version;
    e.srcVersion = src->version;
    e.sig = sig;
    e.out = out;
    return out;
}

unsigned DetectKeyColor(const MediaFrame& f)
{
    if (f.width < 8 || f.height < 8)
        return 0x00FF00;
    // أطراف الصورة (فوق وعلى الجنبين): غالباً هي الستارة
    std::vector<int> cbs, crs;
    std::vector<unsigned> cols;
    auto sample = [&](int x, int y)
    {
        const uint8_t* p = f.pixels.data() + ((size_t)y * f.width + x) * 4;
        cbs.push_back(Cb(p[2], p[1], p[0]));
        crs.push_back(Cr(p[2], p[1], p[0]));
        cols.push_back((p[2] << 16) | (p[1] << 8) | p[0]);
    };
    for (int x = 0; x < f.width; x += std::max(1, f.width / 64))
    {
        sample(x, 2);
        sample(x, f.height / 10);
    }
    for (int y = 0; y < f.height * 3 / 4; y += std::max(1, f.height / 48))
    {
        sample(2, y);
        sample(f.width - 3, y);
    }
    // اللون الأكتر تكرار (الوسيط) ، ومنرجّع متوسط البكسلات القريبة منه
    std::vector<int> a = cbs, b = crs;
    std::nth_element(a.begin(), a.begin() + a.size() / 2, a.end());
    std::nth_element(b.begin(), b.begin() + b.size() / 2, b.end());
    const int mcb = a[a.size() / 2], mcr = b[b.size() / 2];
    long long sr = 0, sg = 0, sb = 0, cnt = 0;
    for (size_t i = 0; i < cols.size(); ++i)
        if (std::abs(cbs[i] - mcb) + std::abs(crs[i] - mcr) < 24)
        {
            sr += (cols[i] >> 16) & 255; sg += (cols[i] >> 8) & 255; sb += cols[i] & 255; ++cnt;
        }
    if (!cnt)
        return 0x00FF00;
    return (unsigned)(((sr / cnt) << 16) | ((sg / cnt) << 8) | (sb / cnt));
}
