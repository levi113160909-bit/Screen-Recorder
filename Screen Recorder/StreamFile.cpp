// =============================================================
//  StreamFile.cpp - شوف StreamFile.h
// =============================================================
#include "StreamFile.h"
#include "Bitstream.h"
#include <mfapi.h>
#include <algorithm>

#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;

bool StreamFileWriter::Open(const std::wstring& path, bool hevc, UINT32 width, UINT32 height, UINT32 fps,
                            UINT32 videoKbps, const std::vector<uint8_t>& config, bool bframes, UINT32 audioKbps)
{
    Close();
    const std::vector<uint8_t> seq = bs::ConfigToAnnexB(config, hevc);
    if (seq.empty())
        return false;
    ComPtr<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(&attrs, 2);
    if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (SUCCEEDED(hr)) hr = attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    ComPtr<IMFSinkWriter> w;
    if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attrs.Get(), &w);

    // الفيديو: مضغوط جاهز (نفس اللي رايح للمنصة)
    ComPtr<IMFMediaType> v;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&v);
    if (SUCCEEDED(hr)) hr = v->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = v->SetGUID(MF_MT_SUBTYPE, hevc ? MFVideoFormat_HEVC : MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = v->SetUINT32(MF_MT_AVG_BITRATE, videoKbps * 1000);
    if (SUCCEEDED(hr)) hr = v->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(v.Get(), MF_MT_FRAME_SIZE, width, height);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(v.Get(), MF_MT_FRAME_RATE, fps, 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(v.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = v->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, seq.data(), (UINT32)seq.size());
    if (SUCCEEDED(hr)) hr = w->AddStream(v.Get(), &m_video);
    if (SUCCEEDED(hr)) hr = w->SetInputMediaType(m_video, v.Get(), nullptr);

    // الصوت: AAC جاهز (48000 ، ستيريو)
    ComPtr<IMFMediaType> a;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&a);
    if (SUCCEEDED(hr)) hr = a->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = a->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    if (SUCCEEDED(hr)) hr = a->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    if (SUCCEEDED(hr)) hr = a->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    if (SUCCEEDED(hr)) hr = a->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(hr)) hr = a->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audioKbps * 1000 / 8);
    if (SUCCEEDED(hr)) hr = a->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);                  // خام
    if (SUCCEEDED(hr)) hr = a->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
    if (SUCCEEDED(hr))
    {
        // بقية HEAACWAVEINFO (12 بايت) + AudioSpecificConfig
        std::vector<uint8_t> ud(12, 0);
        ud[2] = 0x29;                        // wAudioProfileLevelIndication
        const auto asc = AacEncoder::AudioConfig();
        ud.insert(ud.end(), asc.begin(), asc.end());
        hr = a->SetBlob(MF_MT_USER_DATA, ud.data(), (UINT32)ud.size());
    }
    if (SUCCEEDED(hr)) hr = w->AddStream(a.Get(), &m_audio);
    if (SUCCEEDED(hr)) hr = w->SetInputMediaType(m_audio, a.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = w->BeginWriting();
    if (FAILED(hr))
    {
        w.Reset();
        DeleteFileW(path.c_str());
        return false;
    }
    m_writer        = w;
    m_path          = path;
    m_started       = false;
    m_bframes       = bframes;
    m_frameDuration = 10'000'000 / std::max<UINT32>(1, fps);
    return true;
}

void StreamFileWriter::Write(const EncodedPacket& p)
{
    if (!m_writer)
        return;
    if (!m_started)
    {
        if (!p.video || !p.keyframe)
            return;                          // منبلّش من إطار كامل
        m_started = true;
        m_baseMs = p.timeMs;
    }
    if (p.timeMs < m_baseMs)
        return;
    const LONGLONG t = (LONGLONG)(p.timeMs - m_baseMs) * 10'000;

    ComPtr<IMFMediaBuffer> buf;
    ComPtr<IMFSample> s;
    const std::vector<uint8_t>* data = &p.data;
    if (p.video)
    {
        bs::LengthPrefixedToAnnexB(p.data.data(), p.data.size(), m_tmp);
        data = &m_tmp;
    }
    if (data->empty() || FAILED(MFCreateMemoryBuffer((DWORD)data->size(), &buf)))
        return;
    BYTE* dst = nullptr;
    if (FAILED(buf->Lock(&dst, nullptr, nullptr)))
        return;
    memcpy(dst, data->data(), data->size());
    buf->Unlock();
    buf->SetCurrentLength((DWORD)data->size());
    if (FAILED(MFCreateSample(&s)))
        return;
    s->AddBuffer(buf.Get());
    if (p.video)
    {
        s->SetSampleTime(t + (LONGLONG)p.ctsMs * 10'000);        // وقت العرض
        s->SetSampleDuration(m_frameDuration);
        s->SetUINT32(MFSampleExtension_CleanPoint, p.keyframe ? TRUE : FALSE);
        if (m_bframes)
            s->SetUINT64(MFSampleExtension_DecodeTimestamp, (UINT64)t);
        m_writer->WriteSample(m_video, s.Get());
    }
    else
    {
        s->SetSampleTime(t);
        s->SetSampleDuration(1024LL * 10'000'000 / 48000);
        m_writer->WriteSample(m_audio, s.Get());
    }
}

HRESULT StreamFileWriter::Close()
{
    if (!m_writer)
        return S_OK;
    const HRESULT hr = m_started ? m_writer->Finalize() : E_ABORT;
    m_writer.Reset();
    if (FAILED(hr))
        DeleteFileW(m_path.c_str());         // ما انكتب شي مفيد
    return hr;
}
