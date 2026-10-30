// =============================================================
//  NvEnc.h - مشفّر إنفيديا المباشر (NVENC) ، متل برنامج NVIDIA نفسه
//
//  ويندوز بيوصل لمشفّر الكرت من خلال طبقة وسيطة (Media Foundation)
//  وهالطبقة بتقفل أهم ميزات الجودة. هون منحكي مع الكرت مباشرة:
//   - B-frames      : إطارات بتستعين باللي قبلها واللي بعدها (ضغط أذكى)
//   - Lookahead     : المشفّر بيشوف الإطارات الجاية قبل ما يقرّر
//   - AQ (مكاني وزمني): جودة أعلى للمناطق المعقّدة (عشب، شجر، شعر)
//   - تمريرتين (Multi-pass) بإعداد P5 "جودة عالية"
//
//  المدخل: صورة NV12 على كرت الشاشة (من GpuFramePipe)
//  المخرج: قطع مضغوطة (H.264 / HEVC) جاهزة تنكتب بملف MP4
//
//  ما بيحتاج تثبيت شي: الـ DLL جاي مع تعريف إنفيديا.
//  إذا الكرت مش إنفيديا أو التعريف قديم، Open بيرجّع false ومنكمّل بالطريقة العادية.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct NvPacket
{
    std::vector<BYTE> data;     // Annex-B (00 00 00 01 ...)
    LONGLONG pts = 0;           // وقت العرض (100 نانوثانية)
    LONGLONG dts = 0;           // وقت فك الضغط (أبكر من pts مع B-frames)
    LONGLONG duration = 0;
    bool     key = false;       // إطار كامل (IDR)
};

class NvencEncoder
{
public:
    NvencEncoder();
    ~NvencEncoder();

    // hevc=false → H.264 . bframes: 0 أو أكتر (بيتقصّ حسب قدرة الكرت)
    // log: سطور تشخيص (شو انفتح وشو لا)
    // cbr: معدّل ثابت (للبث: المنصات بتحبه) ، وإلا متغيّر (للتسجيل: أحسن جودة)
    // lookahead: كم إطار بيشوف لقدّام (أكتر = أجود بس تأخير أكتر)
    bool Open(ID3D11Device* device, bool hevc, UINT width, UINT height, UINT fps,
              UINT bitrate, int bframes, std::wstring& log, bool cbr = false, int lookahead = 16);
    void Close();

    // VPS/SPS/PPS (Annex-B): لازم لملف MP4
    const std::vector<BYTE>& SequenceHeader() const { return m_seqHeader; }
    int  BFrames() const { return m_bframes; }

    // بينسخ الصورة (NV12) لخانة تبع المشفّر، فالخانة الأصلية بتفضى فوراً
    // packets: اللي طلع جاهز (ممكن صفر مع B-frames و lookahead)
    // forceKey: إطار كامل هلق (مثلاً منصة جديدة اتصلت بالبث)
    bool Encode(ID3D11Texture2D* nv12, LONGLONG pts, LONGLONG duration, std::vector<NvPacket>& packets,
                bool forceKey = false);
    // آخر التسجيل: بيطلّع كل اللي ناطر جوّا المشفّر
    bool Flush(std::vector<NvPacket>& packets);

    // تغيير معدّل البت وهو شغّال (البث: لما يضعف النت). kbps بالكيلوبت
    bool SetBitrate(UINT kbps);
    UINT Bitrate() const { return m_kbps; }

private:
    bool Drain(bool keepDelay, std::vector<NvPacket>& packets);
    std::shared_ptr<void> m_params;     // إعدادات التشغيل (لحتى نعدّل عليها لاحقاً)
    UINT                  m_kbps = 0;
    bool                  m_cbr = false;

    struct Buffer
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        void* registered = nullptr;     // NV_ENC_REGISTERED_PTR
        void* mapped     = nullptr;     // NV_ENC_INPUT_PTR (لما يكون عند المشفّر)
        void* bitstream  = nullptr;     // NV_ENC_OUTPUT_PTR
    };

    HMODULE                                 m_dll = nullptr;
    void*                                   m_api = nullptr;    // NV_ENCODE_API_FUNCTION_LIST*
    void*                                   m_encoder = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    std::vector<Buffer>                     m_buffers;
    std::vector<BYTE>                       m_seqHeader;
    UINT                                    m_width = 0, m_height = 0;
    int                                     m_bframes = 0;
    LONGLONG                                m_frameDuration = 0;
    unsigned long long                      m_sent = 0, m_got = 0;
    std::deque<LONGLONG>                    m_inputPts;         // أوقات الإطارات بترتيب الإدخال (لحساب dts)
    unsigned long long                      m_inputBase = 0;    // رقم أول عنصر بـ m_inputPts
    std::map<LONGLONG, LONGLONG>            m_durations;        // pts ← مدة الإطار
};
