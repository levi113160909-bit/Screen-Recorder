// =============================================================
//  VideoTools.cpp - شوف VideoTools.h
// =============================================================
#include "VideoTools.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <map>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // تصغير بسيط (متوسط مربّعات) من BGRA لـ BGRA
    void Downscale(const BYTE* src, int sw, int sh, int srcPitch, int maxW, std::vector<BYTE>& out, int& ow, int& oh)
    {
        ow = std::min(sw, maxW);
        oh = std::max(1, (int)((long long)sh * ow / sw));
        out.assign((size_t)ow * oh * 4, 0);
        for (int y = 0; y < oh; ++y)
        {
            const int y0 = y * sh / oh, y1 = std::max(y0 + 1, (y + 1) * sh / oh);
            for (int x = 0; x < ow; ++x)
            {
                const int x0 = x * sw / ow, x1 = std::max(x0 + 1, (x + 1) * sw / ow);
                unsigned b = 0, g = 0, r = 0, n = 0;
                const int stepY = std::max(1, (y1 - y0) / 3), stepX = std::max(1, (x1 - x0) / 3);
                for (int yy = y0; yy < y1; yy += stepY)
                    for (int xx = x0; xx < x1; xx += stepX)
                    {
                        const BYTE* p = src + (ptrdiff_t)yy * srcPitch + (ptrdiff_t)xx * 4;
                        b += p[0]; g += p[1]; r += p[2]; ++n;
                    }
                BYTE* d = out.data() + ((size_t)y * ow + x) * 4;
                d[0] = (BYTE)(b / n); d[1] = (BYTE)(g / n); d[2] = (BYTE)(r / n); d[3] = 255;
            }
        }
    }

    struct MfScope
    {
        HRESULT hr;
        MfScope() { hr = MFStartup(MF_VERSION, MFSTARTUP_LITE); }
        ~MfScope() { if (SUCCEEDED(hr)) MFShutdown(); }
    };
}

bool VideoDuration(const std::wstring& path, LONGLONG& duration)
{
    MfScope mf;
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader)))
        return false;
    PROPVARIANT var;
    PropVariantInit(&var);
    bool ok = false;
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) && var.vt == VT_UI8)
    {
        duration = (LONGLONG)var.uhVal.QuadPart;
        ok = true;
    }
    PropVariantClear(&var);
    return ok;
}

bool VideoFrameAt(const std::wstring& path, LONGLONG t, int maxW, std::vector<BYTE>& px, int& w, int& h)
{
    MfScope mf;
    ComPtr<IMFAttributes> attrs;
    MFCreateAttributes(&attrs, 1);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);     // فك الضغط + تحويل لـ RGB
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attrs.Get(), &reader)))
        return false;
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    ComPtr<IMFMediaType> type;
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get())))
        return false;
    ComPtr<IMFMediaType> cur;
    UINT32 fw = 0, fh = 0;
    if (FAILED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur)) ||
        FAILED(MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &fw, &fh)) || !fw || !fh)
        return false;
    if (t > 0)
    {
        PROPVARIANT var;
        InitPropVariantFromInt64(t, &var);
        reader->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
    }
    // بعد القفز منكمّل لحد ما نوصل للوقت المطلوب (أول إطار عنده أو بعده)
    for (int i = 0; i < 600; ++i)
    {
        DWORD flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &sample)))
            return false;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            return false;
        if (!sample || (ts + 400000 < t && i < 599))       // لسا قبل الوقت (بهامش 40ms)
            continue;
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf)))
            return false;
        BYTE* data = nullptr;
        DWORD len = 0;
        if (FAILED(buf->Lock(&data, nullptr, &len)))
            return false;
        LONG stride = (LONG)fw * 4;
        UINT32 defStride = 0;
        if (SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &defStride)))
            stride = (LONG)(INT32)defStride;
        const BYTE* top = stride < 0 ? data + (size_t)(-stride) * (fh - 1) : data;
        if (len >= (DWORD)(std::abs(stride) * (LONG)fh))
            Downscale(top, (int)fw, (int)fh, stride, maxW, px, w, h);
        buf->Unlock();
        return !px.empty();
    }
    return false;
}

HRESULT TrimVideo(const std::wstring& in, const std::wstring& out, LONGLONG start, LONGLONG end,
                  std::atomic<int>* progress, std::atomic<bool>* cancel)
{
    MfScope mf;
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(in.c_str(), nullptr, &reader);     // بلا فك ضغط
    if (FAILED(hr))
        return hr;
    ComPtr<IMFSinkWriter> writer;
    hr = MFCreateSinkWriterFromURL(out.c_str(), nullptr, nullptr, &writer);
    if (FAILED(hr))
        return hr;

    // كل مسار (فيديو + كل مسارات الصوت) بينقل متل ما هو
    std::map<DWORD, DWORD> map;                 // مسار القراءة ← مسار الكتابة
    DWORD videoIn = (DWORD)-1;
    for (DWORD i = 0;; ++i)
    {
        ComPtr<IMFMediaType> type;
        hr = reader->GetNativeMediaType(i, 0, &type);
        if (hr == MF_E_INVALIDSTREAMNUMBER)
            break;
        if (FAILED(hr))
            return hr;
        GUID major{};
        type->GetMajorType(&major);
        if (major != MFMediaType_Video && major != MFMediaType_Audio)
        {
            reader->SetStreamSelection(i, FALSE);
            continue;
        }
        DWORD o = 0;
        if (FAILED(writer->AddStream(type.Get(), &o)) || FAILED(writer->SetInputMediaType(o, type.Get(), nullptr)))
        {
            reader->SetStreamSelection(i, FALSE);
            continue;
        }
        reader->SetStreamSelection(i, TRUE);
        map[i] = o;
        if (major == MFMediaType_Video && videoIn == (DWORD)-1)
            videoIn = i;
    }
    if (map.empty() || videoIn == (DWORD)-1)
        return E_FAIL;
    hr = writer->BeginWriting();
    if (FAILED(hr))
        return hr;

    PROPVARIANT var;
    InitPropVariantFromInt64(std::max<LONGLONG>(0, start), &var);
    reader->SetCurrentPosition(GUID_NULL, var);              // بيوقف عند الإطار الكامل اللي قبل
    PropVariantClear(&var);

    LONGLONG base = -1;
    size_t done = 0;
    std::map<DWORD, bool> finished;
    while (done < map.size())
    {
        if (cancel && *cancel)
            return E_ABORT;
        DWORD idx = 0, flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample((DWORD)MF_SOURCE_READER_ANY_STREAM, 0, &idx, &flags, &ts, &sample);
        if (FAILED(hr))
            return hr;
        auto it = map.find(idx);
        if (it == map.end())
            continue;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            if (!finished[idx]) { finished[idx] = true; ++done; }
            continue;
        }
        if (!sample || finished[idx])
            continue;
        if (idx == videoIn && base < 0)
        {
            UINT32 key = 0;
            if (FAILED(sample->GetUINT32(MFSampleExtension_CleanPoint, &key)) || !key)
                continue;                                  // منبلّش من إطار كامل
            base = ts;
        }
        if (base < 0 || ts < base)
            continue;                                      // صوت قبل أول إطار
        if (ts > end)
        {
            finished[idx] = true;
            ++done;
            reader->SetStreamSelection(idx, FALSE);
            continue;
        }
        sample->SetSampleTime(ts - base);
        UINT64 dts = 0;
        if (SUCCEEDED(sample->GetUINT64(MFSampleExtension_DecodeTimestamp, &dts)))
            sample->SetUINT64(MFSampleExtension_DecodeTimestamp, (UINT64)std::max<LONGLONG>(0, (LONGLONG)dts - base));
        hr = writer->WriteSample(it->second, sample.Get());
        if (FAILED(hr))
            return hr;
        if (progress && idx == videoIn && end > start)
            *progress = (int)std::clamp<LONGLONG>((ts - start) * 100 / (end - start), 0, 99);
    }
    hr = writer->Finalize();
    if (progress && SUCCEEDED(hr))
        *progress = 100;
    return hr;
}

bool ShellThumbnail(const std::wstring& path, int size, std::vector<BYTE>& px, int& w, int& h)
{
    ComPtr<IShellItemImageFactory> f;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&f))))
    {
        HBITMAP bmp = nullptr;
        if (SUCCEEDED(f->GetImage(SIZE{ size, size * 9 / 16 }, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &bmp)) && bmp)
        {
            BITMAP bm{};
            GetObjectW(bmp, sizeof(bm), &bm);
            w = bm.bmWidth;
            h = std::abs(bm.bmHeight);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;                  // من فوق لتحت
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            px.resize((size_t)w * h * 4);
            HDC dc = GetDC(nullptr);
            const int lines = GetDIBits(dc, bmp, 0, h, px.data(), &bi, DIB_RGB_COLORS);
            ReleaseDC(nullptr, dc);
            DeleteObject(bmp);
            if (lines == h)
            {
                for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
                return true;
            }
        }
    }
    // ويندوز ما عطانا (مثلاً HEVC بلا إضافة الترميز): منجرّب نفك الفيديو بنفسنا
    LONGLONG dur = 0;
    VideoDuration(path, dur);
    return VideoFrameAt(path, std::min<LONGLONG>(10'000'000, dur / 3), size, px, w, h);
}
