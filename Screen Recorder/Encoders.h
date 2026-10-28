// =============================================================
//  Encoders.h - المشفّرات للبث المباشر
//
//  بالتسجيل كان SinkWriter بيعمل كل شي لحالو ويكتب ملف.
//  بالبث بدنا "القطع المضغوطة" نفسها لحتى نبعتها عالإنترنت،
//  فمنشغّل المشفّرات بإيدنا:
//   - H264Encoder: صورة -> فيديو H.264
//   - AacEncoder : صوت  -> AAC
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>

// قطعة مضغوطة جاهزة للإرسال
struct EncodedPacket
{
    bool                 video = true;
    bool                 keyframe = false;   // إطار كامل (منه بيقدر المشاهد يبلّش)
    uint32_t             timeMs = 0;
    std::vector<uint8_t> data;               // فيديو: NALUs بطول قبل كل وحدة (AVCC) ، صوت: AAC خام
};

// من صورة BGRA لصيغة NV12 (اللي بيفهمها مشفّر الفيديو)
void BgraToNv12(const uint8_t* bgra, int width, int height, uint8_t* nv12);

class H264Encoder
{
public:
    ~H264Encoder() { Shutdown(); }

    // hardware: نحاول بطاقة الرسومات أول؟
    HRESULT Init(UINT32 width, UINT32 height, UINT32 fps, UINT32 kbps, bool tryHardware, bool allowSoftware);
    void    Shutdown();

    // بيرمّز صورة وحدة (NV12)، والقطع الجاهزة بتنضاف لـ out
    HRESULT Encode(const uint8_t* nv12, LONGLONG time100ns, bool forceKeyframe, std::vector<EncodedPacket>& out);

    // إعدادات الفك (SPS/PPS) بصيغة AVCDecoderConfigurationRecord - فاضية لحد أول إطار كامل
    std::vector<uint8_t> AvcConfig() const { return m_avcConfig; }
    std::wstring Name() const { return m_name; }

private:
    HRESULT Create(bool hardware);
    HRESULT Configure();
    HRESULT PullOutputs(std::vector<EncodedPacket>& out);                       // للمشفّرات العادية
    HRESULT PumpEvents(std::vector<EncodedPacket>& out, DWORD waitInputMs);    // للمشفّرات اللي بتشتغل بالأحداث
    HRESULT ProcessOneOutput(std::vector<EncodedPacket>& out);
    void    HandleOutput(IMFSample* sample, std::vector<EncodedPacket>& out);

    Microsoft::WRL::ComPtr<IMFTransform>          m_mft;
    Microsoft::WRL::ComPtr<IMFMediaEventGenerator> m_events;   // للمشفّرات اللي بتشتغل "على الأحداث" (بطاقة الرسومات)
    bool         m_async = false;
    int          m_needInput = 0;
    UINT32       m_width = 0, m_height = 0, m_fps = 30, m_kbps = 6000;
    std::wstring m_name;
    std::vector<uint8_t> m_avcConfig;
};

class AacEncoder
{
public:
    ~AacEncoder() { Shutdown(); }
    HRESULT Init(UINT32 kbps);     // 48000 ، ستيريو
    void    Shutdown();
    // PCM 16 بت ستيريو
    HRESULT Encode(const int16_t* pcm, size_t frames, std::vector<EncodedPacket>& out);

    // إعدادات الفك (AudioSpecificConfig) لـ AAC-LC 48000 ستيريو
    static std::vector<uint8_t> AudioConfig() { return { 0x11, 0x90 }; }

private:
    HRESULT Pull(std::vector<EncodedPacket>& out);
    Microsoft::WRL::ComPtr<IMFTransform> m_mft;
    uint64_t m_outFrames = 0;      // كم إطار AAC طلع (كل واحد 1024 عينة)
};
