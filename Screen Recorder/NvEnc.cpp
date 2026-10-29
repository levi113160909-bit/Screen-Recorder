// =============================================================
//  NvEnc.cpp - مشفّر إنفيديا المباشر (شوف NvEnc.h)
// =============================================================
#include "NvEnc.h"
#include "nvEncodeAPI.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace
{
    typedef NVENCSTATUS (NVENCAPI* PCreateInstance)(NV_ENCODE_API_FUNCTION_LIST*);
    typedef NVENCSTATUS (NVENCAPI* PGetMaxVersion)(uint32_t*);

    void Add(std::wstring& log, const wchar_t* fmt, ...)
    {
        wchar_t buf[300];
        va_list args;
        va_start(args, fmt);
        vswprintf_s(buf, fmt, args);
        va_end(args);
        log += L"   ";
        log += buf;
        log += L"\r\n";
    }
}

#define API (static_cast<NV_ENCODE_API_FUNCTION_LIST*>(m_api))

NvencEncoder::NvencEncoder() = default;
NvencEncoder::~NvencEncoder() { Close(); }

bool NvencEncoder::Open(ID3D11Device* device, bool hevc, UINT width, UINT height, UINT fps,
                        UINT bitrate, int bframes, std::wstring& log, bool cbr, int lookaheadWanted)
{
    Close();
    if (!device || width < 64 || height < 64 || fps == 0)
        return false;
    m_device = device;
    m_device->GetImmediateContext(&m_context);
    m_width  = width & ~1u;
    m_height = height & ~1u;
    m_frameDuration = 10'000'000LL / fps;

    // ---- المكتبة (جاية مع تعريف إنفيديا) ----
    m_dll = LoadLibraryW(L"nvEncodeAPI64.dll");
    if (!m_dll)
    {
        Add(log, L"NVENC: ما في مكتبة إنفيديا (الكرت مش إنفيديا؟)");
        return false;
    }
    auto getMax = (PGetMaxVersion)GetProcAddress(m_dll, "NvEncodeAPIGetMaxSupportedVersion");
    auto create = (PCreateInstance)GetProcAddress(m_dll, "NvEncodeAPICreateInstance");
    uint32_t maxVer = 0;
    if (!getMax || !create || getMax(&maxVer) != NV_ENC_SUCCESS)
    {
        Add(log, L"NVENC: المكتبة ناقصة");
        Close();
        return false;
    }
    const uint32_t needVer = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
    if (maxVer < needVer)
    {
        Add(log, L"NVENC: تعريف إنفيديا قديم (بيدعم %u.%u ، لازم %u.%u أو أحدث) - حدّث التعريف",
            maxVer >> 4, maxVer & 15, (UINT)NVENCAPI_MAJOR_VERSION, (UINT)NVENCAPI_MINOR_VERSION);
        Close();
        return false;
    }
    auto* fl = new NV_ENCODE_API_FUNCTION_LIST{};
    fl->version = NV_ENCODE_API_FUNCTION_LIST_VER;
    m_api = fl;
    if (create(fl) != NV_ENC_SUCCESS)
    {
        Add(log, L"NVENC: ما قدرنا نجهّز المكتبة");
        Close();
        return false;
    }

    // ---- الجلسة على نفس جهاز كرت الشاشة تبع المركّب ----
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sp{};
    sp.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    sp.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    sp.device     = m_device.Get();
    sp.apiVersion = NVENCAPI_VERSION;
    NVENCSTATUS st = API->nvEncOpenEncodeSessionEx(&sp, &m_encoder);
    if (st != NV_ENC_SUCCESS)
    {
        Add(log, L"NVENC: فتح الجلسة فشل (%d) - يمكن الكرت مش إنفيديا أو كل الجلسات مشغولة", (int)st);
        m_encoder = nullptr;
        Close();
        return false;
    }

    const GUID codec = hevc ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
    {
        uint32_t count = 0;
        API->nvEncGetEncodeGUIDCount(m_encoder, &count);
        std::vector<GUID> guids(count);
        uint32_t got = 0;
        if (count) API->nvEncGetEncodeGUIDs(m_encoder, guids.data(), count, &got);
        if (std::find(guids.begin(), guids.begin() + got, codec) == guids.begin() + got)
        {
            Add(log, L"NVENC: الكرت ما بيدعم %s", hevc ? L"HEVC" : L"H.264");
            Close();
            return false;
        }
    }
    auto caps = [&](NV_ENC_CAPS what) -> int
    {
        NV_ENC_CAPS_PARAM p{};
        p.version = NV_ENC_CAPS_PARAM_VER;
        p.capsToQuery = what;
        int v = 0;
        return API->nvEncGetEncodeCaps(m_encoder, codec, &p, &v) == NV_ENC_SUCCESS ? v : 0;
    };
    const int  maxB      = caps(NV_ENC_CAPS_NUM_MAX_BFRAMES);
    const bool lookahead = caps(NV_ENC_CAPS_SUPPORT_LOOKAHEAD) != 0;
    const bool temporalAQ = caps(NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ) != 0;
    m_bframes = std::clamp(bframes, 0, std::max(0, maxB));

    // ---- الإعداد: P5 "جودة عالية" (أو أسرع شوي إذا الدقة × الفريمات كبار كتير) ----
    const double pixelsPerSec = (double)m_width * m_height * fps;
    GUID preset = NV_ENC_PRESET_P5_GUID;
    const wchar_t* presetName = L"P5";
    if (pixelsPerSec > 2560.0 * 1440 * 150)     { preset = NV_ENC_PRESET_P3_GUID; presetName = L"P3"; }
    else if (pixelsPerSec > 2560.0 * 1440 * 90) { preset = NV_ENC_PRESET_P4_GUID; presetName = L"P4"; }

    NV_ENC_PRESET_CONFIG pc{};
    pc.version = NV_ENC_PRESET_CONFIG_VER;
    pc.presetCfg.version = NV_ENC_CONFIG_VER;
    st = API->nvEncGetEncodePresetConfigEx(m_encoder, codec, preset, NV_ENC_TUNING_INFO_HIGH_QUALITY, &pc);
    if (st != NV_ENC_SUCCESS)
    {
        Add(log, L"NVENC: ما قدرنا نجيب إعدادات الجودة (%d)", (int)st);
        Close();
        return false;
    }
    NV_ENC_CONFIG cfg = pc.presetCfg;
    cfg.version        = NV_ENC_CONFIG_VER;
    cfg.profileGUID    = hevc ? NV_ENC_HEVC_PROFILE_MAIN_GUID : NV_ENC_H264_PROFILE_HIGH_GUID;
    cfg.gopLength      = std::max<UINT>(30, fps * 2);          // إطار كامل كل ثانيتين
    cfg.frameIntervalP = m_bframes + 1;

    const int laDepth = lookahead ? std::clamp(lookaheadWanted, 0, 31 - m_bframes) : 0;
    NV_ENC_RC_PARAMS& rc = cfg.rcParams;
    if (cbr)
    {
        // البث: معدّل ثابت ومخزن ثانية وحدة (متل ما بتنصح يوتيوب وتويتش)
        rc.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        rc.averageBitRate  = bitrate;
        rc.maxBitRate      = bitrate;
        rc.vbvBufferSize   = bitrate;
        rc.vbvInitialDelay = bitrate;
    }
    else
    {
        rc.rateControlMode = NV_ENC_PARAMS_RC_VBR;
        rc.averageBitRate  = bitrate;
        rc.maxBitRate      = bitrate * 2;                     // المشاهد الصعبة بتاخد لضعف
        rc.vbvBufferSize   = bitrate * 2;
        rc.vbvInitialDelay = bitrate * 2;
    }
    rc.multiPass        = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
    rc.enableAQ         = 1;
    rc.aqStrength       = 0;                                  // تلقائي
    rc.enableTemporalAQ = temporalAQ ? 1 : 0;
    rc.enableLookahead  = laDepth > 0 ? 1 : 0;
    rc.lookaheadDepth   = (uint16_t)laDepth;

    // ألوان BT.709 (16-235) بالملف نفسه: متل التحويل اللي بيعمله GpuFramePipe
    auto setVui = [](NV_ENC_CONFIG_H264_VUI_PARAMETERS& v)
    {
        v.videoSignalTypePresentFlag   = 1;
        v.videoFormat                  = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
        v.videoFullRangeFlag           = 0;
        v.colourDescriptionPresentFlag = 1;
        v.colourPrimaries              = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
        v.transferCharacteristics      = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
        v.colourMatrix                 = NV_ENC_VUI_MATRIX_COEFFS_BT709;
    };
    if (hevc)
    {
        auto& h = cfg.encodeCodecConfig.hevcConfig;
        h.idrPeriod       = cfg.gopLength;
        h.repeatSPSPPS    = 1;
        h.chromaFormatIDC = 1;
        setVui(h.hevcVUIParameters);
    }
    else
    {
        auto& h = cfg.encodeCodecConfig.h264Config;
        h.idrPeriod       = cfg.gopLength;
        h.repeatSPSPPS    = 1;
        h.chromaFormatIDC = 1;
        setVui(h.h264VUIParameters);
    }

    NV_ENC_INITIALIZE_PARAMS ip{};
    ip.version           = NV_ENC_INITIALIZE_PARAMS_VER;
    ip.encodeGUID        = codec;
    ip.presetGUID        = preset;
    ip.tuningInfo        = NV_ENC_TUNING_INFO_HIGH_QUALITY;
    ip.encodeWidth       = m_width;
    ip.encodeHeight      = m_height;
    ip.darWidth          = m_width;
    ip.darHeight         = m_height;
    ip.maxEncodeWidth    = m_width;
    ip.maxEncodeHeight   = m_height;
    ip.frameRateNum      = fps;
    ip.frameRateDen      = 1;
    ip.enablePTD         = 1;
    ip.enableEncodeAsync = 0;
    ip.encodeConfig      = &cfg;
    st = API->nvEncInitializeEncoder(m_encoder, &ip);
    if (st != NV_ENC_SUCCESS && m_bframes > 0)
    {
        // احتياط: بلا B-frames
        Add(log, L"NVENC: الإعداد مع B-frames ما زبط (%d)، منجرّب بدونها", (int)st);
        m_bframes = 0;
        cfg.frameIntervalP = 1;
        rc.lookaheadDepth = (uint16_t)(lookahead ? std::clamp(lookaheadWanted, 0, 31) : 0);
        rc.enableLookahead = rc.lookaheadDepth > 0 ? 1 : 0;
        st = API->nvEncInitializeEncoder(m_encoder, &ip);
    }
    if (st != NV_ENC_SUCCESS)
    {
        Add(log, L"NVENC: تجهيز المشفّر فشل (%d)", (int)st);
        Close();
        return false;
    }

    // ---- الخانات: صورة دخل + مكان للناتج لكل إطار ممكن يكون "جوّا" المشفّر ----
    const int count = rc.lookaheadDepth + m_bframes + 1 + 3;
    for (int i = 0; i < count; ++i)
    {
        Buffer b;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = m_width;  d.Height = m_height;
        d.MipLevels = 1;    d.ArraySize = 1;
        d.Format = DXGI_FORMAT_NV12;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(m_device->CreateTexture2D(&d, nullptr, &b.tex)))
        {
            Add(log, L"NVENC: ما في ذاكرة كافية بكرت الشاشة");
            Close();
            return false;
        }
        NV_ENC_REGISTER_RESOURCE rr{};
        rr.version            = NV_ENC_REGISTER_RESOURCE_VER;
        rr.resourceType       = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        rr.width              = m_width;
        rr.height             = m_height;
        rr.pitch              = 0;
        rr.resourceToRegister = b.tex.Get();
        rr.bufferFormat       = NV_ENC_BUFFER_FORMAT_NV12;
        rr.bufferUsage        = NV_ENC_INPUT_IMAGE;
        st = API->nvEncRegisterResource(m_encoder, &rr);
        if (st != NV_ENC_SUCCESS)
        {
            Add(log, L"NVENC: تسجيل الصور فشل (%d)", (int)st);
            Close();
            return false;
        }
        b.registered = rr.registeredResource;

        NV_ENC_CREATE_BITSTREAM_BUFFER cb{};
        cb.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        st = API->nvEncCreateBitstreamBuffer(m_encoder, &cb);
        if (st != NV_ENC_SUCCESS)
        {
            API->nvEncUnregisterResource(m_encoder, b.registered);
            Add(log, L"NVENC: مكان الناتج فشل (%d)", (int)st);
            Close();
            return false;
        }
        b.bitstream = cb.bitstreamBuffer;
        m_buffers.push_back(std::move(b));
    }

    // ---- SPS/PPS (و VPS لـ HEVC) ----
    std::vector<BYTE> seq(1024);
    uint32_t seqSize = 0;
    NV_ENC_SEQUENCE_PARAM_PAYLOAD pp{};
    pp.version              = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
    pp.inBufferSize         = (uint32_t)seq.size();
    pp.spsppsBuffer         = seq.data();
    pp.outSPSPPSPayloadSize = &seqSize;
    st = API->nvEncGetSequenceParams(m_encoder, &pp);
    if (st != NV_ENC_SUCCESS || seqSize == 0)
    {
        Add(log, L"NVENC: ما قدرنا نجيب ترويسة الفيديو (%d)", (int)st);
        Close();
        return false;
    }
    seq.resize(seqSize);
    m_seqHeader = std::move(seq);

    Add(log, L"NVENC ✓  %s  %s  %s  B-frames=%d (أقصى %d)  Lookahead=%d  AQ مكاني ✓  AQ زمني %s  تمريرتين ✓",
        hevc ? L"HEVC" : L"H.264", presetName, cbr ? L"CBR" : L"VBR", m_bframes, maxB, (int)rc.lookaheadDepth,
        temporalAQ ? L"✓" : L"✗");
    return true;
}

void NvencEncoder::Close()
{
    if (m_api && m_encoder)
    {
        for (Buffer& b : m_buffers)
        {
            if (b.mapped)     API->nvEncUnmapInputResource(m_encoder, b.mapped);
            if (b.registered) API->nvEncUnregisterResource(m_encoder, b.registered);
            if (b.bitstream)  API->nvEncDestroyBitstreamBuffer(m_encoder, b.bitstream);
        }
        API->nvEncDestroyEncoder(m_encoder);
    }
    m_buffers.clear();
    m_encoder = nullptr;
    delete API;
    m_api = nullptr;
    if (m_dll)
        FreeLibrary(m_dll);
    m_dll = nullptr;
    m_seqHeader.clear();
    m_inputPts.clear();
    m_durations.clear();
    m_sent = m_got = m_inputBase = 0;
}

bool NvencEncoder::Encode(ID3D11Texture2D* nv12, LONGLONG pts, LONGLONG duration, std::vector<NvPacket>& packets,
                          bool forceKey)
{
    if (!m_encoder || !nv12)
        return false;
    Buffer& b = m_buffers[m_sent % m_buffers.size()];

    // نسخة على كرت الشاشة نفسه (سريعة كتير) لحتى نرجّع الخانة الأصلية فوراً
    m_context->CopyResource(b.tex.Get(), nv12);

    NV_ENC_MAP_INPUT_RESOURCE map{};
    map.version            = NV_ENC_MAP_INPUT_RESOURCE_VER;
    map.registeredResource = b.registered;
    if (API->nvEncMapInputResource(m_encoder, &map) != NV_ENC_SUCCESS)
        return false;
    b.mapped = map.mappedResource;

    NV_ENC_PIC_PARAMS pic{};
    pic.version         = NV_ENC_PIC_PARAMS_VER;
    pic.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;
    pic.inputBuffer     = map.mappedResource;
    pic.bufferFmt       = map.mappedBufferFmt;
    pic.inputWidth      = m_width;
    pic.inputHeight     = m_height;
    pic.outputBitstream = b.bitstream;
    pic.inputTimeStamp  = (uint64_t)pts;
    pic.inputDuration   = (uint64_t)duration;
    if (forceKey)
        pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    const NVENCSTATUS st = API->nvEncEncodePicture(m_encoder, &pic);
    if (st != NV_ENC_SUCCESS && st != NV_ENC_ERR_NEED_MORE_INPUT)
    {
        API->nvEncUnmapInputResource(m_encoder, b.mapped);
        b.mapped = nullptr;
        return false;
    }
    m_inputPts.push_back(pts);
    m_durations[pts] = duration;
    ++m_sent;
    return Drain(true, packets);
}

bool NvencEncoder::Flush(std::vector<NvPacket>& packets)
{
    if (!m_encoder)
        return false;
    NV_ENC_PIC_PARAMS pic{};
    pic.version        = NV_ENC_PIC_PARAMS_VER;
    pic.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    API->nvEncEncodePicture(m_encoder, &pic);
    return Drain(false, packets);
}

bool NvencEncoder::Drain(bool keepDelay, std::vector<NvPacket>& packets)
{
    // منترك آخر كم خانة ليخلص المشفّر منهم (B-frames و lookahead)؛ القفل بيستنى لحد ما يجهز
    const unsigned long long delay = keepDelay ? m_buffers.size() - 1 : 0;
    const unsigned long long end   = m_sent > delay ? m_sent - delay : 0;
    for (; m_got < end; ++m_got)
    {
        Buffer& b = m_buffers[m_got % m_buffers.size()];
        NV_ENC_LOCK_BITSTREAM lock{};
        lock.version         = NV_ENC_LOCK_BITSTREAM_VER;
        lock.outputBitstream = b.bitstream;
        if (API->nvEncLockBitstream(m_encoder, &lock) != NV_ENC_SUCCESS)
            return false;

        NvPacket p;
        const BYTE* src = static_cast<const BYTE*>(lock.bitstreamBufferPtr);
        p.data.assign(src, src + lock.bitstreamSizeInBytes);
        p.pts = (LONGLONG)lock.outputTimeStamp;
        p.key = lock.pictureType == NV_ENC_PIC_TYPE_IDR;
        API->nvEncUnlockBitstream(m_encoder, b.bitstream);

        if (b.mapped)
        {
            API->nvEncUnmapInputResource(m_encoder, b.mapped);
            b.mapped = nullptr;
        }

        // dts: وقت الإطار رقم (k - عدد B-frames) بترتيب الإدخال. هيك دايماً dts ≤ pts وبتزيد
        const unsigned long long k = m_got;
        const unsigned long long B = (unsigned long long)m_bframes;
        if (k >= B)
            p.dts = m_inputPts[(size_t)(k - B - m_inputBase)];
        else
            p.dts = m_inputPts[0] - (LONGLONG)(B - k) * m_frameDuration;

        auto it = m_durations.find(p.pts);
        p.duration = it != m_durations.end() ? it->second : m_frameDuration;
        if (it != m_durations.end())
            m_durations.erase(it);

        // منحذف الأوقات القديمة اللي ما رح نحتاجها
        // (الإطار الجاي k+1 بيحتاج العنصر رقم k+1-B ، فاللي قبله ما عاد إله لزوم)
        if (k + 1 >= B)
            while (m_inputBase < k + 1 - B && m_inputPts.size() > 1)
            {
                m_inputPts.pop_front();
                ++m_inputBase;
            }
        if (!p.data.empty())
            packets.push_back(std::move(p));
    }
    return true;
}
