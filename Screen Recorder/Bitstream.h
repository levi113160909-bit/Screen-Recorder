// =============================================================
//  Bitstream.h - أدوات صغيرة للفيديو المضغوط (H.264 و HEVC)
//
//  المشفّر بيطلّع الفيديو بصيغة "Annex B": كل وحدة (NAL) قبلها 00 00 00 01.
//  المنصات (RTMP/FLV) وملفات MP4 بدهم كل وحدة قبلها طولها (4 بايت)،
//  وبدهم "بطاقة تعريف" للفيديو بالأول:
//    H.264 → avcC  (فيها SPS و PPS)
//    HEVC  → hvcC  (فيها VPS و SPS و PPS)
//
//  وكمان قطع FLV للبث:
//    H.264 : الطريقة القديمة (كل المنصات)
//    HEVC  : "Enhanced RTMP" (يوتيوب وغيرها) - الترميز بينكتب كـ 'hvc1'
//
//  هالملف ما بيعتمد على ويندوز (منجرّبه ونفحصه لحاله).
// =============================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace bs
{
    // الوحدات (NAL units) بدون 00 00 01 قبلها
    std::vector<std::pair<const uint8_t*, size_t>> SplitAnnexB(const uint8_t* p, size_t n);

    // من Annex B لوحدات قبلها طولها، بدون إعدادات التسلسل والفواصل (بتنبعت مرة بالأول)
    // بيرجّع true إذا فيها إطار كامل (IDR / IRAP)
    bool ToLengthPrefixed(const uint8_t* p, size_t n, bool hevc, std::vector<uint8_t>& out);

    // بطاقات التعريف من SPS/PPS (و VPS) بصيغة Annex B. فاضية إذا ناقص شي
    std::vector<uint8_t> BuildAvcC(const uint8_t* p, size_t n);
    std::vector<uint8_t> BuildHvcC(const uint8_t* p, size_t n);

    // جسم قطعة فيديو FLV (للبث)
    // header: بطاقة التعريف (أول شي بعد الاتصال)
    std::vector<uint8_t> FlvVideoHeader(bool hevc, const std::vector<uint8_t>& config);
    // إطار: ctsMs = الفرق بين وقت العرض ووقت فك الضغط (مع B-frames)
    void FlvVideoFrame(bool hevc, bool key, int32_t ctsMs, const uint8_t* data, size_t n, std::vector<uint8_t>& out);

    // رقم الترميز بمعلومات البث (onMetaData): H.264 = 7 ، HEVC = 'hvc1'
    double FlvVideoCodecId(bool hevc);
}
