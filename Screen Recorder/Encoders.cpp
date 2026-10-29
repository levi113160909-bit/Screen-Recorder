// =============================================================
//  Encoders.cpp
// =============================================================
#include "Encoders.h"

#include <mferror.h>
#include <strmif.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")

using Microsoft::WRL::ComPtr;

// =============================================================
//  BGRA -> NV12  (الإضاءة Y لكل بكسل ، واللون UV لكل 2×2 بكسل)
// =============================================================
void BgraToNv12(const uint8_t* bgra, int width, int height, uint8_t* nv12)
{
    uint8_t* yPlane  = nv12;
    uint8_t* uvPlane = nv12 + (size_t)width * height;

    for (int y = 0; y < height; y += 2)
    {
        const uint8_t* row0 = bgra + (size_t)y * width * 4;
        const uint8_t* row1 = (y + 1 < height) ? row0 + (size_t)width * 4 : row0;
        uint8_t* y0  = yPlane + (size_t)y * width;
        uint8_t* y1  = (y + 1 < height) ? y0 + width : nullptr;
        uint8_t* uv  = uvPlane + (size_t)(y / 2) * width;

        for (int x = 0; x < width; x += 2)
        {
            int rs = 0, gs = 0, bs = 0;
            for (int k = 0; k < 2; ++k)
            {
                const int xi = std::min(x + k, width - 1);
                const uint8_t* p0 = row0 + xi * 4;
                const uint8_t* p1 = row1 + xi * 4;
                // معادلات BT.601 المعروفة
                y0[xi] = (uint8_t)(((66 * p0[2] + 129 * p0[1] + 25 * p0[0] + 128) >> 8) + 16);
                if (y1)
                    y1[xi] = (uint8_t)(((66 * p1[2] + 129 * p1[1] + 25 * p1[0] + 128) >> 8) + 16);
                rs += p0[2] + p1[2];
                gs += p0[1] + p1[1];
                bs += p0[0] + p1[0];
            }
            rs /= 4; gs /= 4; bs /= 4;
            uv[x]     = (uint8_t)(((-38 * rs - 74 * gs + 112 * bs + 128) >> 8) + 128);   // U
            uv[x + 1] = (uint8_t)(((112 * rs - 94 * gs - 18 * bs + 128) >> 8) + 128);   // V
        }
    }
}

namespace
{
    // بيقسم فيديو H.264 (Annex B) لوحداته (NAL units)
    std::vector<std::pair<const uint8_t*, size_t>> SplitNals(const uint8_t* p, size_t n)
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
            while (end > begin && p[end - 1] == 0)
                --end;
            if (end > begin)
                nals.push_back({ p + begin, end - begin });
        }
        return nals;
    }

    void SetCodec(ICodecAPI* codec, const GUID& key, UINT32 value)
    {
        VARIANT v;
        VariantInit(&v);
        v.vt    = VT_UI4;
        v.ulVal = value;
        codec->SetValue(&key, &v);
    }

    ComPtr<IMFSample> MakeSample(const void* data, DWORD size, LONGLONG time, LONGLONG duration)
    {
        ComPtr<IMFMediaBuffer> buf;
        ComPtr<IMFSample> sample;
        if (FAILED(MFCreateMemoryBuffer(size, &buf)))
            return nullptr;
        BYTE* dst = nullptr;
        if (FAILED(buf->Lock(&dst, nullptr, nullptr)))
            return nullptr;
        memcpy(dst, data, size);
        buf->Unlock();
        buf->SetCurrentLength(size);
        if (FAILED(MFCreateSample(&sample)))
            return nullptr;
        sample->AddBuffer(buf.Get());
        sample->SetSampleTime(time);
        sample->SetSampleDuration(duration);
        return sample;
    }
}

// =============================================================
//  H.264
// =============================================================
HRESULT H264Encoder::Init(UINT32 width, UINT32 height, UINT32 fps, UINT32 kbps, bool tryHardware, bool allowSoftware)
{
    Shutdown();
    m_width = width & ~1;
    m_height = height & ~1;
    m_fps = fps;
    m_kbps = kbps;

    HRESULT hr = E_FAIL;
    if (tryHardware)
    {
        hr = Create(true);
        if (SUCCEEDED(hr)) hr = Configure();
        if (FAILED(hr)) Shutdown();
    }
    if (FAILED(hr) && allowSoftware)
    {
        hr = Create(false);
        if (SUCCEEDED(hr)) hr = Configure();
        if (FAILED(hr)) Shutdown();
    }
    return hr;
}

void H264Encoder::Shutdown()
{
    if (m_mft)
    {
        m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    }
    m_events.Reset();
    m_mft.Reset();
    m_async = false;
    m_needInput = 0;
    m_avcConfig.clear();
}

HRESULT H264Encoder::Create(bool hardware)
{
    MFT_REGISTER_TYPE_INFO in{ MFMediaType_Video, MFVideoFormat_NV12 };
    MFT_REGISTER_TYPE_INFO out{ MFMediaType_Video, MFVideoFormat_H264 };
    const UINT32 flags = (hardware ? MFT_ENUM_FLAG_HARDWARE : MFT_ENUM_FLAG_SYNCMFT) | MFT_ENUM_FLAG_SORTANDFILTER;

    IMFActivate** acts = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, &in, &out, &acts, &count);
    if (FAILED(hr) || count == 0)
    {
        if (acts) CoTaskMemFree(acts);
        return FAILED(hr) ? hr : MF_E_TOPO_CODEC_NOT_FOUND;
    }

    WCHAR* name = nullptr;
    UINT32 len = 0;
    m_name = hardware ? L"" : L"Microsoft H.264 (المعالج)";
    if (SUCCEEDED(acts[0]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &len)))
    {
        m_name = name;
        CoTaskMemFree(name);
    }
    hr = acts[0]->ActivateObject(IID_PPV_ARGS(&m_mft));
    for (UINT32 i = 0; i < count; ++i)
        acts[i]->Release();
    CoTaskMemFree(acts);
    if (FAILED(hr))
        return hr;

    // مشفّرات بطاقة الرسومات "غير متزامنة": لازم نفتح قفلها ونشتغل معها بالأحداث
    ComPtr<IMFAttributes> attrs;
    if (SUCCEEDED(m_mft->GetAttributes(&attrs)))
    {
        UINT32 isAsync = FALSE;
        attrs->GetUINT32(MF_TRANSFORM_ASYNC, &isAsync);
        if (isAsync)
        {
            attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
            if (FAILED(m_mft.As(&m_events)))
                return E_NOINTERFACE;
            m_async = true;
        }
    }
    return S_OK;
}

HRESULT H264Encoder::Configure()
{
    // الشكل اللي بيطلع: H.264
    ComPtr<IMFMediaType> outType;
    HRESULT hr = MFCreateMediaType(&outType);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, m_kbps * 1000);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, m_width, m_height);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, m_fps, 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
    if (SUCCEEDED(hr)) hr = m_mft->SetOutputType(0, outType.Get(), 0);

    // الشكل اللي بيدخل: NV12
    ComPtr<IMFMediaType> inType;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inType);
    if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, m_width, m_height);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, m_fps, 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = m_mft->SetInputType(0, inType.Get(), 0);
    if (FAILED(hr))
        return hr;

    // إعدادات مناسبة للبث: معدّل بت ثابت، إطار كامل كل ثانيتين، بلا B-frames، تأخير قليل
    ComPtr<ICodecAPI> codec;
    if (SUCCEEDED(m_mft.As(&codec)))
    {
        SetCodec(codec.Get(), CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
        SetCodec(codec.Get(), CODECAPI_AVEncCommonMeanBitRate, m_kbps * 1000);
        SetCodec(codec.Get(), CODECAPI_AVEncMPVGOPSize, m_fps * 2);
        SetCodec(codec.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0);
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_BOOL;
        v.boolVal = VARIANT_TRUE;
        codec->SetValue(&CODECAPI_AVLowLatencyMode, &v);
    }

    m_mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return S_OK;
}

HRESULT H264Encoder::Encode(const uint8_t* nv12, LONGLONG time100ns, bool forceKeyframe, std::vector<EncodedPacket>& out)
{
    if (!m_mft)
        return E_UNEXPECTED;

    const DWORD size = m_width * m_height * 3 / 2;
    ComPtr<IMFSample> sample = MakeSample(nv12, size, time100ns, 10'000'000 / m_fps);
    if (!sample)
        return E_OUTOFMEMORY;

    if (forceKeyframe)
    {
        ComPtr<ICodecAPI> codec;
        if (SUCCEEDED(m_mft.As(&codec)))
            SetCodec(codec.Get(), CODECAPI_AVEncVideoForceKeyFrame, 1);
    }

    if (!m_async)
    {
        HRESULT hr = m_mft->ProcessInput(0, sample.Get(), 0);
        if (hr == MF_E_NOTACCEPTING)
        {
            PullOutputs(out);
            hr = m_mft->ProcessInput(0, sample.Get(), 0);
        }
        if (FAILED(hr))
            return hr;
        return PullOutputs(out);
    }

    // بطاقة الرسومات: منستنى لحد ما تطلب صورة (أقصى شي 50ms، وإلا منتخطّى الصورة)
    HRESULT hr = PumpEvents(out, 50);
    if (FAILED(hr))
        return hr;
    if (m_needInput <= 0)
        return S_FALSE;                 // مشغولة: منتخطّى هالصورة
    hr = m_mft->ProcessInput(0, sample.Get(), 0);
    if (FAILED(hr))
        return hr;
    --m_needInput;
    return PumpEvents(out, 0);
}

HRESULT H264Encoder::PumpEvents(std::vector<EncodedPacket>& out, DWORD waitInputMs)
{
    const ULONGLONG deadline = GetTickCount64() + waitInputMs;
    for (;;)
    {
        ComPtr<IMFMediaEvent> ev;
        HRESULT hr = m_events->GetEvent(MF_EVENT_FLAG_NO_WAIT, &ev);
        if (hr == MF_E_NO_EVENTS_AVAILABLE)
        {
            if (m_needInput <= 0 && GetTickCount64() < deadline)
            {
                Sleep(1);
                continue;
            }
            return S_OK;
        }
        if (FAILED(hr))
            return hr;

        MediaEventType type = MEUnknown;
        ev->GetType(&type);
        if (type == METransformNeedInput)
            ++m_needInput;
        else if (type == METransformHaveOutput)
        {
            hr = ProcessOneOutput(out);
            if (FAILED(hr) && hr != MF_E_TRANSFORM_NEED_MORE_INPUT)
                return hr;
        }
    }
}

HRESULT H264Encoder::PullOutputs(std::vector<EncodedPacket>& out)
{
    for (;;)
    {
        const HRESULT hr = ProcessOneOutput(out);
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
            return S_OK;
        if (FAILED(hr))
            return hr;
    }
}

HRESULT H264Encoder::ProcessOneOutput(std::vector<EncodedPacket>& out)
{
    MFT_OUTPUT_STREAM_INFO info{};
    m_mft->GetOutputStreamInfo(0, &info);

    MFT_OUTPUT_DATA_BUFFER ob{};
    ComPtr<IMFSample> own;
    if (!(info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)))
    {
        ComPtr<IMFMediaBuffer> buf;
        const DWORD cb = std::max<DWORD>(info.cbSize, m_width * m_height);
        if (FAILED(MFCreateMemoryBuffer(cb, &buf)) || FAILED(MFCreateSample(&own)))
            return E_OUTOFMEMORY;
        own->AddBuffer(buf.Get());
        ob.pSample = own.Get();
    }

    DWORD status = 0;
    HRESULT hr = m_mft->ProcessOutput(0, 1, &ob, &status);
    if (ob.pEvents)
        ob.pEvents->Release();

    if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
    {
        // المشفّر غيّر شكل المخرج: منقبل الشكل الجديد
        ComPtr<IMFMediaType> type;
        if (SUCCEEDED(m_mft->GetOutputAvailableType(0, 0, &type)))
            m_mft->SetOutputType(0, type.Get(), 0);
        if (ob.pSample && ob.pSample != own.Get())
            ob.pSample->Release();
        return S_OK;
    }
    if (SUCCEEDED(hr) && ob.pSample)
        HandleOutput(ob.pSample, out);
    if (ob.pSample && ob.pSample != own.Get())
        ob.pSample->Release();
    return hr;
}

void H264Encoder::HandleOutput(IMFSample* sample, std::vector<EncodedPacket>& out)
{
    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf)))
        return;
    BYTE* p = nullptr;
    DWORD len = 0;
    if (FAILED(buf->Lock(&p, nullptr, &len)))
        return;

    LONGLONG time = 0;
    sample->GetSampleTime(&time);
    UINT32 clean = FALSE;
    sample->GetUINT32(MFSampleExtension_CleanPoint, &clean);

    EncodedPacket pkt;
    pkt.video    = true;
    pkt.timeMs   = (uint32_t)(time / 10'000);
    pkt.keyframe = clean != FALSE;

    std::vector<uint8_t> sps, pps;
    for (const auto& [nal, size] : SplitNals(p, len))
    {
        const int type = nal[0] & 0x1F;
        if (type == 7) { sps.assign(nal, nal + size); continue; }   // إعدادات التسلسل
        if (type == 8) { pps.assign(nal, nal + size); continue; }   // إعدادات الصورة
        if (type == 9) continue;                                     // فاصل (ما منحتاجه)
        if (type == 5) pkt.keyframe = true;
        // كل وحدة: طولها (4 بايت) وبعدين محتواها
        pkt.data.push_back((uint8_t)(size >> 24));
        pkt.data.push_back((uint8_t)(size >> 16));
        pkt.data.push_back((uint8_t)(size >> 8));
        pkt.data.push_back((uint8_t)size);
        pkt.data.insert(pkt.data.end(), nal, nal + size);
    }
    buf->Unlock();

    // أول مرة منشوف SPS و PPS منبني "بطاقة التعريف" تبع الفيديو
    if (!sps.empty() && !pps.empty() && sps.size() >= 4)
    {
        std::vector<uint8_t> cfg = { 1, sps[1], sps[2], sps[3], 0xFF, 0xE1 };
        cfg.push_back((uint8_t)(sps.size() >> 8));
        cfg.push_back((uint8_t)sps.size());
        cfg.insert(cfg.end(), sps.begin(), sps.end());
        cfg.push_back(1);
        cfg.push_back((uint8_t)(pps.size() >> 8));
        cfg.push_back((uint8_t)pps.size());
        cfg.insert(cfg.end(), pps.begin(), pps.end());
        m_avcConfig = cfg;
    }

    if (!pkt.data.empty())
        out.push_back(std::move(pkt));
}

void AnnexBToPacket(const uint8_t* p, size_t len, LONGLONG time100ns, bool keyframe,
                    EncodedPacket& pkt, std::vector<uint8_t>* avcConfig)
{
    pkt.video    = true;
    pkt.timeMs   = (uint32_t)(time100ns / 10'000);
    pkt.keyframe = keyframe;
    pkt.data.clear();
    std::vector<uint8_t> sps, pps;
    for (const auto& [nal, size] : SplitNals(p, len))
    {
        const int type = nal[0] & 0x1F;
        if (type == 7) { sps.assign(nal, nal + size); continue; }
        if (type == 8) { pps.assign(nal, nal + size); continue; }
        if (type == 9 || type == 6) continue;                      // فاصل / معلومات إضافية
        if (type == 5) pkt.keyframe = true;
        pkt.data.push_back((uint8_t)(size >> 24));
        pkt.data.push_back((uint8_t)(size >> 16));
        pkt.data.push_back((uint8_t)(size >> 8));
        pkt.data.push_back((uint8_t)size);
        pkt.data.insert(pkt.data.end(), nal, nal + size);
    }
    if (avcConfig && sps.size() >= 4 && !pps.empty())
    {
        std::vector<uint8_t> cfg = { 1, sps[1], sps[2], sps[3], 0xFF, 0xE1 };
        cfg.push_back((uint8_t)(sps.size() >> 8));
        cfg.push_back((uint8_t)sps.size());
        cfg.insert(cfg.end(), sps.begin(), sps.end());
        cfg.push_back(1);
        cfg.push_back((uint8_t)(pps.size() >> 8));
        cfg.push_back((uint8_t)pps.size());
        cfg.insert(cfg.end(), pps.begin(), pps.end());
        *avcConfig = cfg;
    }
}

// =============================================================
//  AAC
// =============================================================
HRESULT AacEncoder::Init(UINT32 kbps)
{
    Shutdown();
    HRESULT hr = CoCreateInstance(CLSID_AACMFTEncoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_mft));
    if (FAILED(hr))
        return hr;

    ComPtr<IMFMediaType> in, out;
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 4);

    // AAC بيقبل بس هالقيم: 12000 / 16000 / 20000 / 24000 بايت بالثانية
    UINT32 bytesPerSec = 20000;
    if (kbps <= 96) bytesPerSec = 12000; else if (kbps <= 128) bytesPerSec = 16000; else if (kbps > 160) bytesPerSec = 24000;

    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytesPerSec);
    out->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);                       // AAC خام (بلا رؤوس)
    out->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);

    hr = m_mft->SetInputType(0, in.Get(), 0);
    if (SUCCEEDED(hr))
        hr = m_mft->SetOutputType(0, out.Get(), 0);
    if (FAILED(hr))     // بعض النسخ بدها الترتيب العكسي
    {
        hr = m_mft->SetOutputType(0, out.Get(), 0);
        if (SUCCEEDED(hr))
            hr = m_mft->SetInputType(0, in.Get(), 0);
    }
    if (FAILED(hr))
        return hr;

    m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    m_outFrames = 0;
    return S_OK;
}

void AacEncoder::Shutdown()
{
    m_mft.Reset();
}

HRESULT AacEncoder::Encode(const int16_t* pcm, size_t frames, std::vector<EncodedPacket>& out)
{
    if (!m_mft)
        return E_UNEXPECTED;
    ComPtr<IMFSample> sample = MakeSample(pcm, (DWORD)(frames * 4), 0, 0);
    if (!sample)
        return E_OUTOFMEMORY;
    HRESULT hr = m_mft->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING)
    {
        Pull(out);
        hr = m_mft->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr))
        return hr;
    return Pull(out);
}

HRESULT AacEncoder::Pull(std::vector<EncodedPacket>& out)
{
    for (;;)
    {
        MFT_OUTPUT_STREAM_INFO info{};
        m_mft->GetOutputStreamInfo(0, &info);
        ComPtr<IMFMediaBuffer> buf;
        ComPtr<IMFSample> sample;
        MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, 8192), &buf);
        MFCreateSample(&sample);
        sample->AddBuffer(buf.Get());

        MFT_OUTPUT_DATA_BUFFER ob{};
        ob.pSample = sample.Get();
        DWORD status = 0;
        const HRESULT hr = m_mft->ProcessOutput(0, 1, &ob, &status);
        if (ob.pEvents)
            ob.pEvents->Release();
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
            return S_OK;
        if (FAILED(hr))
            return hr;

        BYTE* p = nullptr;
        DWORD len = 0;
        if (SUCCEEDED(buf->Lock(&p, nullptr, &len)))
        {
            if (len > 0)
            {
                EncodedPacket pkt;
                pkt.video    = false;
                pkt.keyframe = true;
                pkt.timeMs   = (uint32_t)(m_outFrames * 1024 * 1000 / 48000);
                pkt.data.assign(p, p + len);
                out.push_back(std::move(pkt));
                ++m_outFrames;
            }
            buf->Unlock();
        }
    }
}
