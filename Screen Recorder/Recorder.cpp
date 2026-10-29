// =============================================================
//  Recorder.cpp - هون بيصير التسجيل الحقيقي
// =============================================================
#include "Recorder.h"

#include "GpuEncode.h"
#include "NvEnc.h"
#include <mmsystem.h>      // timeBeginPeriod: توقيت أدق
#include <mfapi.h>
#include <mfidl.h>
#include <algorithm>
#include <cstring>
// إعدادات المشفّر (CODECAPI_...): initguid لحتى تنعرّف أرقامها هون
#include <initguid.h>
#include <codecapi.h>
#include <strmif.h>       // ICodecAPI: منكلّم المشفّر مباشرة

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "winmm.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // Media Foundation بتحسب الوقت بوحدة 100 نانو ثانية
    constexpr LONGLONG ONE_SECOND     = 10'000'000;

    // الصوت بيتأخر 100ms عن الوقت الحقيقي، لحتى يكون في وقت يوصل من الجهاز
    constexpr size_t AUDIO_LAG_FRAMES = AudioSource::SAMPLE_RATE / 10;

    LONGLONG QpcNow()
    {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return t.QuadPart;
    }

    // AV1 (بالـ SDK القديم ما في اسم إله)
    const GUID kAV1 = { FCC('AV01'), 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

    struct WriterConfig
    {
        std::wstring          path;
        UINT32                width = 0, height = 0, fps = 30, bitrate = 8'000'000;
        int                   audioTracks = 0;
        bool                  hardware = true, crashSafe = true;
        VideoCodec            codec = VideoCodec::H264;
        IMFDXGIDeviceManager* gpu = nullptr;      // موجود = الطريق السريع (الصور NV12 على كرت الشاشة)
        bool                  tuned = true;       // إعدادات جودة إضافية للمشفّر
        // موجود = الفيديو جاي مضغوط جاهز (من NVENC): منكتبه متل ما هو بلا مشفّر ويندوز
        const std::vector<BYTE>* passthroughHeader = nullptr;
    };

    // ألوان الفيديو القياسية (BT.709 ، 16-235): متل كل الفيديوهات، فالمشغّلات بتعرضها صح
    void SetVideoColors(IMFMediaType* t)
    {
        t->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
        t->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
        t->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
        t->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    }

    // ---------------------------------------------------------
    // منجهّز "الكاتب": ملف MP4 فيه فيديو (H.264 / HEVC / AV1) + صوت AAC
    // ---------------------------------------------------------
    // آخر خطوة وصلها CreateWriter (لسجل التشخيص)
    const wchar_t* g_writerStep = L"";
    std::wstring   g_codecLog;          // شو قبل المشفّر من إعدادات الجودة

    // منبعت إعدادات الجودة للمشفّر مباشرة (أضمن من تمريرها مع النوع: بعض المشفّرات بتتجاهلها هونيك)
    void TuneEncoder(IMFSinkWriter* writer, DWORD stream, const WriterConfig& c)
    {
        ComPtr<ICodecAPI> api;
        if (FAILED(writer->GetServiceForStream(stream, GUID_NULL, IID_PPV_ARGS(&api))))
        {
            g_codecLog += L"   المشفّر ما بيسمح بتعديل إعداداته\r\n";
            return;
        }
        auto set = [&](const GUID& key, UINT32 value, const wchar_t* name)
        {
            VARIANT v; VariantInit(&v);
            v.vt = VT_UI4; v.ulVal = value;
            HRESULT hr = api->IsSupported(&key);
            if (hr == S_OK) hr = api->SetValue(&key, &v);
            else if (hr == S_FALSE) hr = E_NOTIMPL;
            wchar_t buf[160];
            swprintf_s(buf, L"   %s = %u  →  %s\r\n", name, value, SUCCEEDED(hr) ? L"✓" : L"✗ مرفوض");
            g_codecLog += buf;
        };
        set(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_PeakConstrainedVBR, L"نوع التحكم بالبت ريت");
        set(CODECAPI_AVEncCommonMeanBitRate,     c.bitrate,                L"البت ريت");
        set(CODECAPI_AVEncCommonMaxBitRate,      c.bitrate * 2,            L"أقصى بت ريت");
        set(CODECAPI_AVEncCommonQualityVsSpeed,  85,                       L"الجودة مقابل السرعة");
        set(CODECAPI_AVEncMPVGOPSize,            std::max<UINT32>(30, c.fps * 2), L"إطار كامل كل");
        if (c.codec != VideoCodec::AV1)
            set(CODECAPI_AVEncMPVDefaultBPictureCount, 2,                  L"B-frames");
        set(CODECAPI_AVLowLatencyMode,           0,                        L"وضع التأخير المنخفض");
    }

    HRESULT CreateWriter(const WriterConfig& c, IMFSinkWriter** ppWriter, DWORD* pVideo, std::vector<DWORD>* pAudio)
    {
        g_writerStep = L"إنشاء الملف";
        ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 4);
        // MP4 "مقطّع": بيكتب الفيديو على شكل قطع صغيرة، فإذا البرنامج ضرب
        // أو انطفى الجهاز بيضل كل شي قبلها شغّال
        if (SUCCEEDED(hr) && c.crashSafe)
            hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_FMPEG4);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, c.hardware || c.gpu);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        if (SUCCEEDED(hr) && c.gpu)
            hr = attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, c.gpu);

        ComPtr<IMFSinkWriter> writer;
        if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(c.path.c_str(), nullptr, attributes.Get(), &writer);

        // ---------- الفيديو ----------
        const GUID subtype = c.codec == VideoCodec::HEVC ? MFVideoFormat_HEVC
                           : c.codec == VideoCodec::AV1  ? kAV1 : MFVideoFormat_H264;
        ComPtr<IMFMediaType> vOut;
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&vOut);
        if (SUCCEEDED(hr)) hr = vOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = vOut->SetGUID(MF_MT_SUBTYPE, subtype);
        if (SUCCEEDED(hr)) hr = vOut->SetUINT32(MF_MT_AVG_BITRATE, c.bitrate);
        if (SUCCEEDED(hr)) hr = vOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(vOut.Get(), MF_MT_FRAME_SIZE, c.width, c.height);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(vOut.Get(), MF_MT_FRAME_RATE, c.fps, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(vOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        // "High" بدل الافتراضي (Baseline): نفس معدّل البت بس صورة أوضح بكتير
        if (SUCCEEDED(hr) && c.codec == VideoCodec::H264) hr = vOut->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
        if (SUCCEEDED(hr) && c.codec == VideoCodec::HEVC && c.tuned) hr = vOut->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH265VProfile_Main_420_8);
        if (SUCCEEDED(hr)) SetVideoColors(vOut.Get());
        if (SUCCEEDED(hr) && c.passthroughHeader)
            hr = vOut->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, c.passthroughHeader->data(), (UINT32)c.passthroughHeader->size());
        DWORD video = 0;
        if (SUCCEEDED(hr)) { g_writerStep = L"إضافة مسار الفيديو (الحاوية ما قبلت الترميز؟)"; hr = writer->AddStream(vOut.Get(), &video); }

        // اللي منعطيه: الطريق السريع NV12 على كرت الشاشة ، وإلا صور RGB32 من تحت لفوق
        ComPtr<IMFMediaType> vIn;
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&vIn);
        if (SUCCEEDED(hr)) hr = vIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = vIn->SetGUID(MF_MT_SUBTYPE, c.gpu ? MFVideoFormat_NV12 : MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = vIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr) && !c.gpu) hr = vIn->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(-(LONG)(c.width * 4)));
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(vIn.Get(), MF_MT_FRAME_SIZE, c.width, c.height);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(vIn.Get(), MF_MT_FRAME_RATE, c.fps, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(vIn.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(hr) && c.gpu) SetVideoColors(vIn.Get());

        // إعدادات الجودة للمشفّر (متل NVIDIA): جودة متغيّرة بحد أعلى، أنواع إطارات أذكى،
        // وأفضلية للجودة على السرعة. إذا المشفّر ما قبلها منجرّب بدونها (تحت)
        ComPtr<IMFAttributes> enc;
        if (SUCCEEDED(hr) && c.tuned)
        {
            MFCreateAttributes(&enc, 8);
            enc->SetUINT32(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_PeakConstrainedVBR);
            enc->SetUINT32(CODECAPI_AVEncCommonMeanBitRate, c.bitrate);
            enc->SetUINT32(CODECAPI_AVEncCommonMaxBitRate, c.bitrate * 2);
            enc->SetUINT32(CODECAPI_AVEncCommonQualityVsSpeed, 85);
            enc->SetUINT32(CODECAPI_AVEncMPVGOPSize, std::max<UINT32>(30, c.fps * 2));     // إطار كامل كل ثانيتين
            if (c.codec != VideoCodec::AV1)
                enc->SetUINT32(CODECAPI_AVEncMPVDefaultBPictureCount, 2);
        }
        if (SUCCEEDED(hr)) { g_writerStep = L"تجهيز المشفّر (ما في مشفّر قبل الصيغة)"; hr = c.passthroughHeader ? writer->SetInputMediaType(video, vOut.Get(), nullptr)      // مضغوط جاهز
                                                                                  : writer->SetInputMediaType(video, vIn.Get(), enc.Get()); }
        g_codecLog.clear();
        if (SUCCEEDED(hr) && c.tuned && !c.passthroughHeader) TuneEncoder(writer.Get(), video, c);

        // ---------- الصوت (مسار أو أكتر، كلهم بنفس الإعدادات) ----------
        std::vector<DWORD> audioStreams;
        if (SUCCEEDED(hr)) g_writerStep = L"مسارات الصوت";
        for (int track = 0; track < c.audioTracks && SUCCEEDED(hr); ++track)
        {
            DWORD audio = 0;
            ComPtr<IMFMediaType> aOut;   // AAC: 48000، ستيريو، 192 كيلوبت
            if (SUCCEEDED(hr)) hr = MFCreateMediaType(&aOut);
            if (SUCCEEDED(hr)) hr = aOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = aOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            if (SUCCEEDED(hr)) hr = aOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = aOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AudioSource::SAMPLE_RATE);
            if (SUCCEEDED(hr)) hr = aOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, AudioSource::CHANNELS);
            if (SUCCEEDED(hr)) hr = aOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
            if (SUCCEEDED(hr)) hr = writer->AddStream(aOut.Get(), &audio);

            ComPtr<IMFMediaType> aIn;    // اللي منعطيه: PCM 16 بت
            if (SUCCEEDED(hr)) hr = MFCreateMediaType(&aIn);
            if (SUCCEEDED(hr)) hr = aIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = aIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AudioSource::SAMPLE_RATE);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, AudioSource::CHANNELS);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, AudioSource::CHANNELS * 2);
            if (SUCCEEDED(hr)) hr = aIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AudioSource::SAMPLE_RATE * AudioSource::CHANNELS * 2);
            if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audio, aIn.Get(), nullptr);
            audioStreams.push_back(audio);
        }

        if (SUCCEEDED(hr)) { g_writerStep = L"بدء الكتابة"; hr = writer->BeginWriting(); }
        if (SUCCEEDED(hr)) g_writerStep = L"تمام";
        if (SUCCEEDED(hr))
        {
            *ppWriter = writer.Detach();
            *pVideo = video;
            *pAudio = audioStreams;
        }
        return hr;
    }

    // بعد ما يشتغل الكاتب، منسأله: مين المشفّر اللي اختاره فعلاً؟
    std::wstring QueryEncoderName(IMFSinkWriter* writer, DWORD stream)
    {
        ComPtr<IMFSinkWriterEx> ex;
        if (FAILED(writer->QueryInterface(IID_PPV_ARGS(&ex))))
            return L"";

        for (DWORD i = 0;; ++i)
        {
            GUID category{};
            ComPtr<IMFTransform> transform;
            if (FAILED(ex->GetTransformForStream(stream, i, &category, &transform)))
                break;
            if (category != MFT_CATEGORY_VIDEO_ENCODER)
                continue;

            ComPtr<IMFAttributes> attrs;
            if (SUCCEEDED(transform->GetAttributes(&attrs)))
            {
                WCHAR* name = nullptr;
                UINT32 len = 0;
                if (SUCCEEDED(attrs->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &len)))
                {
                    std::wstring result = name;
                    CoTaskMemFree(name);
                    return result;
                }
            }
            return L"Microsoft H.264 (CPU)";   // المشفّر البرمجي ما إلو اسم
        }
        return L"";
    }
}

std::vector<Recorder::EncoderInfo> Recorder::ListEncoders()
{
    std::vector<EncoderInfo> list;
    MFT_REGISTER_TYPE_INFO output{ MFMediaType_Video, MFVideoFormat_H264 };

    // مرتين: أول شي بطاقات الرسومات، بعدين البرمجي (المعالج)
    for (int pass = 0; pass < 2; ++pass)
    {
        const UINT32 flags = (pass == 0 ? MFT_ENUM_FLAG_HARDWARE : (MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT))
                           | MFT_ENUM_FLAG_SORTANDFILTER;
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, nullptr, &output, &activates, &count)))
            continue;

        for (UINT32 i = 0; i < count; ++i)
        {
            WCHAR* name = nullptr;
            UINT32 len = 0;
            if (SUCCEEDED(activates[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &len)))
            {
                list.push_back({ name, pass == 0 });
                CoTaskMemFree(name);
            }
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
    }
    return list;
}

// =============================================================
//  البداية والنهاية
// =============================================================
Recorder::Recorder() = default;

Recorder::~Recorder()
{
    Stop();
}

HRESULT Recorder::Start(const RecordOptions& options)
{
    Stop();
    m_opt = options;

    if (!m_opt.scene)
        return E_INVALIDARG;
    {
        std::lock_guard<std::mutex> lock(m_opt.scene->mutex);
        m_width  = m_opt.scene->canvasW & ~1;      // H.264 بدها أرقام زوجية
        m_height = m_opt.scene->canvasH & ~1;
    }
    if (m_width < 16 || m_height < 16)
        return E_INVALIDARG;

    m_hasAudio      = !m_opt.audio.empty();
    // المخلوط دايماً، ومسار لكل مصدر إذا طلبتهم (وفي أكتر من مصدر، وإلا ما في داعي)
    const int audioTracks = !m_hasAudio ? 0
                          : (m_opt.separateTracks && m_opt.audio.size() > 1 ? 1 + (int)m_opt.audio.size() : 1);
    m_fps           = std::clamp<UINT32>(m_opt.fps, 1, 240);
    m_frameDuration = ONE_SECOND / m_fps;
    const UINT32 bitrate = std::clamp<UINT32>(m_opt.bitrate, 500'000, 200'000'000);

    // ---- المركّب (منجهّزه هون لحتى نعرف جهاز كرت الشاشة قبل ما نفتح الكاتب) ----
    m_comp = std::make_unique<Compositor>(true);          // true = منرسم الماوس
    m_pipe.reset();
    const bool tryHardware = (m_opt.encoder != EncoderMode::Software);
    if (tryHardware && !m_opt.cpuPath && m_comp->EnsureGpu())
    {
        // الطريق السريع: التحويل والضغط على كرت الشاشة
        m_pipe = std::make_unique<GpuFramePipe>();
        if (!m_pipe->Init(m_comp->GpuDevice(), m_width, m_height, m_fps))
            m_pipe.reset();
    }

    // ---- منجرّب بالترتيب لحتى يزبط شي: ----
    //  الطريق السريع ← الطريق العادي ، الترميز المختار ← H.264 ، مع إعدادات الجودة ← بدونها ، كرت الشاشة ← المعالج
    struct Attempt { bool gpu; VideoCodec codec; bool tuned; bool hardware; bool fragmented; };
    std::vector<Attempt> attempts;
    std::vector<VideoCodec> codecs = { m_opt.codec };
    if (m_opt.codec != VideoCodec::H264) codecs.push_back(VideoCodec::H264);
    for (int gpu = (m_pipe ? 1 : 0); gpu >= 0; --gpu)
        for (VideoCodec cdc : codecs)
        {
            // HEVC/AV1: ويندوز ما بيقبلهم بالـ MP4 المقطّع (بيفشل ببدء الكتابة 0xC00D36B4)، فمباشرة MP4 عادي
            const bool frag = m_opt.crashSafe && cdc == VideoCodec::H264;
            for (int tuned = 1; tuned >= 0; --tuned)
                attempts.push_back({ gpu == 1, cdc, tuned == 1, tryHardware, frag });
        }
    if (m_opt.encoder == EncoderMode::Auto)                // تلقائي: آخر شي المعالج
        for (VideoCodec cdc : codecs)
            attempts.push_back({ false, cdc, true, false, m_opt.crashSafe && cdc == VideoCodec::H264 });

    // سجل تشخيص: شو جرّبنا وشو صار (%LOCALAPPDATA%\ScreenRecorder\encoder_log.txt)
    std::wstring log;
    {
        wchar_t buf[200];
        swprintf_s(buf, L"الحجم %ux%u  الفريمات %u  البت ريت %u  الطريق السريع جاهز: %s\r\n",
                   m_width, m_height, m_fps, bitrate, m_pipe ? L"نعم" : L"لا");
        log += buf;
    }

    HRESULT hr = E_FAIL;
    m_nvenc.reset();
    m_videoOffset = 0;

    // ---- أول شي: مشفّر إنفيديا المباشر (أعلى جودة) ----
    //  H.264 مع "MP4 آمن": بلا B-frames (الملف الآمن ما بيدعمهم) بس مع Lookahead و AQ
    //  HEVC أو MP4 عادي: كل شي (B-frames كمان)
    if (m_pipe && tryHardware && m_opt.codec != VideoCodec::AV1)
    {
        const bool hevc = (m_opt.codec == VideoCodec::HEVC);
        struct NvTry { int bframes; bool fragmented; };
        std::vector<NvTry> tries;
        if (m_opt.crashSafe && !hevc) tries.push_back({ 0, true });
        tries.push_back({ 2, false });
        for (const NvTry& t : tries)
        {
            auto nv = std::make_unique<NvencEncoder>();
            if (!nv->Open(m_comp->GpuDevice(), hevc, m_width, m_height, m_fps, bitrate, t.bframes, log))
                break;                                   // ما في NVENC: منكمّل بطريقة ويندوز
            WriterConfig c;
            c.path = m_opt.path; c.width = m_width; c.height = m_height; c.fps = m_fps; c.bitrate = bitrate;
            c.audioTracks = audioTracks; c.hardware = true; c.crashSafe = t.fragmented;
            c.codec = m_opt.codec; c.tuned = false;
            c.passthroughHeader = &nv->SequenceHeader();
            m_writer.Reset();
            hr = CreateWriter(c, &m_writer, &m_videoStream, &m_audioStreams);
            wchar_t buf[200];
            swprintf_s(buf, L"NVENC مباشر | %s  →  0x%08X  (%s)\r\n",
                       t.fragmented ? L"MP4 مقطّع" : L"MP4 عادي", (unsigned)hr, g_writerStep);
            log += buf;
            if (SUCCEEDED(hr))
            {
                m_videoOffset = nv->BFrames() * m_frameDuration;   // لحتى dts ما يكون سالب
                m_nvenc = std::move(nv);
                m_codecUsed = m_opt.codec;
                break;
            }
            m_writer.Reset();
            DeleteFileW(m_opt.path.c_str());
        }
    }

    for (const Attempt& a : attempts)
    {
        if (m_nvenc) break;
        WriterConfig c;
        c.path = m_opt.path; c.width = m_width; c.height = m_height; c.fps = m_fps; c.bitrate = bitrate;
        c.audioTracks = audioTracks; c.hardware = a.hardware; c.crashSafe = a.fragmented;
        c.codec = a.codec; c.tuned = a.tuned;
        c.gpu = a.gpu ? m_pipe->Manager() : nullptr;
        m_writer.Reset();
        hr = CreateWriter(c, &m_writer, &m_videoStream, &m_audioStreams);
        {
            static const wchar_t* names[] = { L"H.264", L"HEVC", L"AV1" };
            wchar_t buf[400];
            swprintf_s(buf, L"%s | %s | %s | %s | %s  →  0x%08X  (%s)\r\n",
                       names[(int)a.codec], a.gpu ? L"كرت الشاشة NV12" : L"عادي RGB",
                       a.tuned ? L"مضبوط" : L"افتراضي", a.hardware ? L"هاردوير" : L"معالج",
                       a.fragmented ? L"MP4 مقطّع" : L"MP4 عادي", (unsigned)hr, g_writerStep);
            log += buf;
            if (SUCCEEDED(hr)) log += g_codecLog;
        }
        if (SUCCEEDED(hr))
        {
            m_codecUsed = a.codec;
            if (!a.gpu)
                m_pipe.reset();                            // الطريق العادي
            break;
        }
        DeleteFileW(m_opt.path.c_str());
    }
    {
        wchar_t dir[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH))
        {
            std::wstring folder = std::wstring(dir) + L"\\ScreenRecorder";
            CreateDirectoryW(folder.c_str(), nullptr);
            HANDLE h = CreateFileW((folder + L"\\encoder_log.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                   nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                const BYTE bom[] = { 0xFF, 0xFE };
                DWORD w = 0;
                WriteFile(h, bom, 2, &w, nullptr);
                WriteFile(h, log.data(), (DWORD)(log.size() * sizeof(wchar_t)), &w, nullptr);
                CloseHandle(h);
            }
        }
    }
    if (FAILED(hr))
    {
        m_writer.Reset();
        m_nvenc.reset();
        m_pipe.reset();
        m_comp.reset();
        DeleteFileW(m_opt.path.c_str());
        return hr;
    }
    m_gpuPath = (m_pipe != nullptr);

    m_encoderName = m_nvenc ? std::wstring(L"NVIDIA NVENC (مباشر)") : QueryEncoderName(m_writer.Get(), m_videoStream);

    // الساعة
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    m_freq        = f.QuadPart;
    m_start       = QpcNow();
    m_pausedTotal = 0;
    m_paused      = false;
    m_error       = S_OK;

    m_taps.clear();
    for (AudioSource* src : m_opt.audio)
    {
        const int tap = src->AddTap();   // مخزن صوت خاص بالتسجيل
        src->SetArmed(tap, true, true);  // بلّش خزّن
        m_taps.push_back(tap);
    }

    m_active  = true;
    m_running = true;
    m_videoThread = std::thread(&Recorder::VideoThread, this);
    if (m_hasAudio)
        m_audioThread = std::thread(&Recorder::AudioThread, this);
    return S_OK;
}

HRESULT Recorder::Stop()
{
    if (!m_active)
        return S_OK;

    m_running = false;
    if (m_videoThread.joinable()) m_videoThread.join();
    if (m_audioThread.joinable()) m_audioThread.join();

    for (size_t i = 0; i < m_opt.audio.size(); ++i)
        m_opt.audio[i]->RemoveTap(m_taps[i]);
    m_taps.clear();

    // منسكّر الملف صح (بدون هالخطوة الفيديو بيخرب)
    HRESULT hr = m_error;
    if (m_writer)
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        const HRESULT hrFinal = m_writer->Finalize();
        if (SUCCEEDED(hr)) hr = hrFinal;
        m_writer.Reset();
    }
    m_nvenc.reset();
    m_pipe.reset();          // بعد ما المشفّر خلص من الصور
    m_comp.reset();

    m_active = false;
    m_paused = false;
    m_opt.audio.clear();
    return hr;
}

void Recorder::SetPaused(bool paused)
{
    if (!m_active)
        return;
    {
        std::lock_guard<std::mutex> lock(m_clockMutex);
        if (paused == m_paused)
            return;
        if (paused)
            m_pauseStart = QpcNow();
        else
            m_pausedTotal += QpcNow() - m_pauseStart;   // منشيل وقت الإيقاف من الحساب
        m_paused = paused;
    }
    // وقت الإيقاف المؤقت ما منخزّن صوت
    for (size_t i = 0; i < m_opt.audio.size(); ++i)
        m_opt.audio[i]->SetArmed(m_taps[i], !paused, false);
}

LONGLONG Recorder::Elapsed100ns()
{
    std::lock_guard<std::mutex> lock(m_clockMutex);
    if (!m_active)
        return 0;
    const LONGLONG now   = m_paused ? m_pauseStart : QpcNow();
    LONGLONG       ticks = now - m_start - m_pausedTotal;
    if (ticks < 0) ticks = 0;
    return (ticks / m_freq) * ONE_SECOND + (ticks % m_freq) * ONE_SECOND / m_freq;
}

// =============================================================
//  الكتابة بالملف
// =============================================================
HRESULT Recorder::WriteVideo(const void* pixels, LONGLONG time, LONGLONG duration)
{
    const DWORD bytes = (DWORD)m_width * m_height * 4;
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);

    BYTE* dest = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&dest, nullptr, nullptr);
    if (SUCCEEDED(hr))
    {
        memcpy(dest, pixels, bytes);
        buffer->Unlock();
        hr = buffer->SetCurrentLength(bytes);
    }

    ComPtr<IMFSample> sample;
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(time);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(duration > 0 ? duration : m_frameDuration);
    if (SUCCEEDED(hr))
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        hr = m_writer->WriteSample(m_videoStream, sample.Get());
    }
    return hr;
}

HRESULT Recorder::WriteAudio(DWORD stream, const float* pcm, size_t frames, LONGLONG time)
{
    const DWORD bytes = (DWORD)(frames * AudioSource::CHANNELS * sizeof(INT16));
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);

    BYTE* dest = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&dest, nullptr, nullptr);
    if (SUCCEEDED(hr))
    {
        // من أرقام عشرية (-1..1) لأرقام 16 بت
        INT16* out = (INT16*)dest;
        for (size_t i = 0; i < frames * AudioSource::CHANNELS; ++i)
        {
            const float v = std::clamp(pcm[i], -1.0f, 1.0f);
            out[i] = (INT16)(v * 32767.0f);
        }
        buffer->Unlock();
        hr = buffer->SetCurrentLength(bytes);
    }

    ComPtr<IMFSample> sample;
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(time);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration((LONGLONG)frames * ONE_SECOND / AudioSource::SAMPLE_RATE);
    if (SUCCEEDED(hr))
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        hr = m_writer->WriteSample(stream, sample.Get());
    }
    return hr;
}

// =============================================================
//  إطار واحد: يا على كرت الشاشة (خانة NV12) يا بالذاكرة (RGB32)
// =============================================================
bool Recorder::ComposeFrame(const std::vector<SceneLayer>& layers, int canvasW, int canvasH, const FadeInfo& fade, Frame& f)
{
    ComposeParams cp;
    cp.outW = m_width; cp.outH = m_height; cp.fade = fade;
    if (m_pipe)
    {
        // الطريق السريع: المركّب بيرجّع الصورة على كرت الشاشة، ومنحوّلها هونيك لـ NV12
        ID3D11Texture2D* tex = nullptr;
        cp.gpuOnly = true;
        cp.outTexture = &tex;
        m_comp->Compose(layers, canvasW, canvasH, cp);
        if (!tex)
            return false;                         // صار خطأ بكرت الشاشة
        f.slot  = m_pipe->Convert(tex);
        f.valid = f.slot >= 0;                    // -1 = المشفّر متأخر: منترك هالإطار
        return true;
    }
    f.cpu.resize((size_t)m_width * m_height * 4);
    cp.out = f.cpu.data(); cp.stride = m_width * 4; cp.bottomUp = true;
    m_comp->Compose(layers, canvasW, canvasH, cp);
    f.valid = true;
    return true;
}

HRESULT Recorder::WriteFrame(Frame& f, LONGLONG time, LONGLONG duration)
{
    if (!f.valid)
        return S_OK;
    f.valid = false;
    if (!m_pipe)
        return WriteVideo(f.cpu.data(), time, duration);
    if (m_nvenc)
    {
        std::vector<NvPacket> packets;
        const bool ok = m_nvenc->Encode(m_pipe->SlotTexture(f.slot), time, duration > 0 ? duration : m_frameDuration, packets);
        m_pipe->Release(f.slot);                 // انتسخت للمشفّر: الخانة فاضية
        f.slot = -1;
        if (!ok)
            return E_FAIL;
        return WriteNvPackets(packets);
    }

    ComPtr<IMFSample> sample;
    HRESULT hr = m_pipe->MakeSample(f.slot, &sample);
    f.slot = -1;
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(time);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(duration > 0 ? duration : m_frameDuration);
    if (SUCCEEDED(hr))
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        hr = m_writer->WriteSample(m_videoStream, sample.Get());
    }
    return hr;
}

HRESULT Recorder::WriteNvPackets(std::vector<NvPacket>& packets)
{
    const bool bframes = m_nvenc && m_nvenc->BFrames() > 0;
    for (NvPacket& p : packets)
    {
        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = MFCreateMemoryBuffer((DWORD)p.data.size(), &buffer);
        BYTE* dest = nullptr;
        if (SUCCEEDED(hr)) hr = buffer->Lock(&dest, nullptr, nullptr);
        if (SUCCEEDED(hr))
        {
            memcpy(dest, p.data.data(), p.data.size());
            buffer->Unlock();
            hr = buffer->SetCurrentLength((DWORD)p.data.size());
        }
        ComPtr<IMFSample> sample;
        if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
        if (SUCCEEDED(hr)) hr = sample->SetSampleTime(p.pts + m_videoOffset);
        if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(p.duration);
        if (SUCCEEDED(hr)) hr = sample->SetUINT32(MFSampleExtension_CleanPoint, p.key ? TRUE : FALSE);
        if (SUCCEEDED(hr) && bframes)            // ترتيب فك الضغط (مع B-frames بيختلف عن ترتيب العرض)
            hr = sample->SetUINT64(MFSampleExtension_DecodeTimestamp, (UINT64)(p.dts + m_videoOffset));
        if (SUCCEEDED(hr))
        {
            std::lock_guard<std::mutex> lock(m_writeMutex);
            hr = m_writer->WriteSample(m_videoStream, sample.Get());
        }
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

void Recorder::DropFrame(Frame& f)
{
    if (m_pipe && f.slot >= 0)
        m_pipe->Release(f.slot);
    f.slot  = -1;
    f.valid = false;
}

// =============================================================
//  خيط الصورة
// =============================================================
void Recorder::VideoThread()
{
    // WinRT (محرّك كرت الشاشة) بدها COM مفعّل بهالخيط
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = S_OK;
    bool endedByItself = false;

    {
        std::vector<SceneLayer> layers;
        int canvasW = 0, canvasH = 0;
        FadeInfo fade;
        Frame frame;

        // تسخين: أول كم صورة من ويندوز بتاخد شوي وقت لتوصل
        for (int i = 0; i < 3; ++i)
        {
            m_opt.scene->Snapshot(canvasW, canvasH, layers, fade);
            ComposeFrame(layers, canvasW, canvasH, fade, frame);
            DropFrame(frame);
            Sleep(20);
        }
        m_usingGpu = m_comp->UsingGpu();

        timeBeginPeriod(1);
        if (m_opt.variableFps)
            VariableFpsLoop(hr, endedByItself);
        else
        {
            LONGLONG last = -1;
            while (m_running)
            {
                if (m_paused)               // إيقاف مؤقت: ما منكتب شي
                {
                    Sleep(10);
                    continue;
                }
                // نسخة من المشهد (يمكن المستخدم عم يحرّك الطبقات هلق)
                m_opt.scene->Snapshot(canvasW, canvasH, layers, fade);
                if (!ComposeFrame(layers, canvasW, canvasH, fade, frame))
                {
                    hr = E_FAIL;            // كرت الشاشة وقف (مثلاً تحديث التعريف)
                    endedByItself = true;
                    break;
                }
                m_usingGpu = m_comp->UsingGpu();

                // الوقت على "الشبكة" بالضبط (0 ، 16.67 ، 33.33 ...) لحتى تطلع الحركة منتظمة
                LONGLONG t = Elapsed100ns() / m_frameDuration * m_frameDuration;
                if (last >= 0 && t <= last) t = last + m_frameDuration;
                hr = WriteFrame(frame, t);
                if (FAILED(hr))
                {
                    endedByItself = true;
                    break;
                }
                last = t;

                // منستنى لوقت الصورة الجاية بدقة: Sleep بيقرّب لتحت، فلو نمنا مرة وحدة
                // منصحى قبل الوقت بشوي ومنكتب صورة زيادة ملزوقة بالجاية (تقطيع خفيف)
                const LONGLONG next = t + m_frameDuration;
                for (;;)
                {
                    const LONGLONG now = Elapsed100ns();
                    if (now >= next || !m_running || m_paused) break;
                    const LONGLONG ms = (next - now) / 10'000;
                    Sleep(ms >= 2 ? (DWORD)(ms - 1) : (ms >= 1 ? 1 : 0));
                }
            }
        }
        DropFrame(frame);
        if (m_nvenc)
        {
            // آخر الإطارات اللي لسا جوّا المشفّر (B-frames و lookahead)
            std::vector<NvPacket> packets;
            m_nvenc->Flush(packets);
            const HRESULT hrTail = WriteNvPackets(packets);
            if (SUCCEEDED(hr) && FAILED(hrTail) && !endedByItself) m_error = hrTail;
        }
        timeEndPeriod(1);
    }

    if (SUCCEEDED(hrCom))
        CoUninitialize();

    if (endedByItself)
    {
        m_error = hr;
        m_running = false;
        PostMessageW(m_opt.notify, WM_RECORDER_DONE, (WPARAM)hr, 0);
    }
}

// =============================================================
//  خيط الصوت: بيخلط المصادر وبيكتب كل 10 ملي ثانية
// =============================================================
void Recorder::AudioThread()
{
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    LONGLONG written = 0;                         // كم عينة كتبنا لهلق
    std::vector<float> mix;
    std::vector<std::vector<float>> each(m_opt.audio.size());    // صوت كل مصدر لحالو
    const bool separate = m_audioStreams.size() > 1;

    while (m_running)
    {
        Sleep(10);

        // كم لازم يكون مكتوب لهلق (متأخرين 100ms عن قصد)
        const LONGLONG elapsedFrames = Elapsed100ns() * AudioSource::SAMPLE_RATE / ONE_SECOND;
        const LONGLONG target        = elapsedFrames - (LONGLONG)AUDIO_LAG_FRAMES;
        if (target <= written)
            continue;

        const size_t frames = (size_t)std::min<LONGLONG>(target - written, AudioSource::SAMPLE_RATE);
        mix.assign(frames * AudioSource::CHANNELS, 0.0f);

        // الخلط = جمع الأصوات فوق بعض
        for (size_t s = 0; s < m_opt.audio.size(); ++s)
        {
            AudioSource* src = m_opt.audio[s];
            std::vector<float>& buf = each[s];
            buf.assign(frames * AudioSource::CHANNELS, 0.0f);   // إذا ما وصل صوت كفاية: الباقي سكوت
            const size_t got = src->Read(m_taps[s], buf.data(), frames);
            for (size_t i = 0; i < got * AudioSource::CHANNELS; ++i)
                mix[i] += buf[i];

            // إذا تجمّع صوت أكتر من اللازم (الساعات مش متطابقة 100%) منرمي القديم
            src->KeepNewest(m_taps[s], AUDIO_LAG_FRAMES * 3);
        }

        const LONGLONG time = written * ONE_SECOND / AudioSource::SAMPLE_RATE;
        HRESULT hr = WriteAudio(m_audioStreams[0], mix.data(), frames, time);
        // المسارات المنفصلة: كل مصدر بمساره (نفس الوقت بالضبط، فبيضلوا متزامنين)
        for (size_t s = 0; separate && SUCCEEDED(hr) && s < each.size() && s + 1 < m_audioStreams.size(); ++s)
            hr = WriteAudio(m_audioStreams[s + 1], each[s].data(), frames, time);
        if (FAILED(hr))
            break;
        written += frames;
    }

    if (SUCCEEDED(hrCom))
        CoUninitialize();
}

// =============================================================
//  الإطارات المتغيّرة ("مثل اللعبة")
//
//  بدل صورة كل 1/fps ثانية، منستنى لحتى تجي صورة جديدة من ويندوز
//  (اللعبة رسمت إطار) ومنكتبها بوقتها الحقيقي. فإذا اللعبة 85 إطار،
//  الفيديو 85، وإذا طلعت 110 الفيديو 110: بلا تكرار وبلا تقطيع.
//
//  ملاحظة: ملف MP4 الآمن بيحسب الوقت من "مدة" كل إطار، فمنأخّر كل إطار
//  لحتى يجي اللي بعده (لنعرف مدته بالضبط). يعني منحتفظ بإطار واحد بالانتظار.
// =============================================================
void Recorder::VariableFpsLoop(HRESULT& hr, bool& endedByItself)
{
    constexpr LONGLONG MAX_FPS   = 240;
    const LONGLONG     minGap    = ONE_SECOND / MAX_FPS;      // ما منكتب أسرع من 240
    const LONGLONG     keepAlive = ONE_SECOND / 10;           // الصورة واقفة: منكرّر كل 0.1 ثانية

    Frame                   pending, current;                 // pending = ناطر مدته
    LONGLONG                pendingTime = -1;
    std::vector<SceneLayer> layers, prevLayers;
    int                     canvasW = 0, canvasH = 0;
    unsigned                prevFadeId = 0;
    FadeInfo                fade;

    // تغيّر شي بالمشهد نفسه؟ (حرّكت طبقة، أخفيت، قصّيت...)
    auto layersChanged = [&]()
    {
        if (layers.size() != prevLayers.size())
            return true;
        for (size_t i = 0; i < layers.size(); ++i)
        {
            const SceneLayer& a = layers[i];
            const SceneLayer& b = prevLayers[i];
            if (a.id != b.id || a.visible != b.visible || a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h ||
                a.cropL != b.cropL || a.cropT != b.cropT || a.cropR != b.cropR || a.cropB != b.cropB)
                return true;
        }
        return false;
    };

    while (m_running)
    {
        if (m_paused)
        {
            Sleep(5);
            continue;
        }
        m_opt.scene->Snapshot(canvasW, canvasH, layers, fade);

        const LONGLONG now = Elapsed100ns();
        if (pendingTime >= 0 && now - pendingTime < minGap)
        {
            Sleep(1);
            continue;
        }
        const bool fading = fade.progress < 1 || fade.id != prevFadeId;
        const bool fresh  = m_comp->PollNewFrames(layers) || fading || layersChanged() || pendingTime < 0;
        if (!fresh && now - pendingTime < keepAlive)
        {
            Sleep(1);                    // ما في شي جديد: منستنى شوي
            continue;
        }

        if (!ComposeFrame(layers, canvasW, canvasH, fade, current))
        {
            hr = E_FAIL;
            endedByItself = true;
            break;
        }
        m_usingGpu = m_comp->UsingGpu();
        prevLayers = layers;
        prevFadeId = fade.id;
        if (!current.valid)
            continue;                    // المشفّر متأخر: منترك هالإطار

        LONGLONG t = Elapsed100ns();
        if (t <= pendingTime) t = pendingTime + 1;

        // هلق منعرف مدة الإطار اللي كان ناطر: منكتبه
        if (pendingTime >= 0)
        {
            hr = WriteFrame(pending, pendingTime, t - pendingTime);
            if (FAILED(hr))
            {
                endedByItself = true;
                break;
            }
        }
        std::swap(pending, current);     // الجديد صار هو الناطر
        pendingTime = t;
    }

    // آخر إطار: مدته لحد هلق
    if (pendingTime >= 0 && pending.valid && !endedByItself)
        hr = WriteFrame(pending, pendingTime, std::max<LONGLONG>(minGap, Elapsed100ns() - pendingTime));
    DropFrame(pending);
    DropFrame(current);
}
