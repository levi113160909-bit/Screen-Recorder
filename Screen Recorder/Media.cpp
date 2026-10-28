// =============================================================
//  Media.cpp - الكاميرا (Media Foundation) والصور (WIC) والنص (DirectWrite)
// =============================================================
#include "Media.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wincodec.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    std::atomic<uint64_t> g_version{ 1 };

    ULONGLONG Now() { return GetTickCount64(); }

    // =========================================================
    //  الكاميرا: بتشتغل بخيط لحالها وبتحفظ آخر صورة
    // =========================================================
    class CameraDevice
    {
    public:
        explicit CameraDevice(const std::wstring& link) : m_link(link)
        {
            m_created  = Now();
            m_lastUsed = m_created;
            m_running  = true;
            m_thread   = std::thread(&CameraDevice::ThreadMain, this);
        }

        ~CameraDevice()
        {
            m_running = false;
            if (m_thread.joinable())
                m_thread.join();
        }

        std::shared_ptr<const MediaFrame> Latest()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_frame;
        }

        bool      Failed() const  { return m_failed; }
        ULONGLONG Created() const { return m_created; }
        std::atomic<ULONGLONG> m_lastUsed{ 0 };

    private:
        void ThreadMain()
        {
            const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            MFStartup(MF_VERSION);
            Run();
            m_reader.Reset();
            if (m_source)
            {
                m_source->Shutdown();
                m_source.Reset();
            }
            MFShutdown();
            if (SUCCEEDED(hrCom))
                CoUninitialize();
        }

        bool Open()
        {
            ComPtr<IMFAttributes> attrs;
            if (FAILED(MFCreateAttributes(&attrs, 2)))
                return false;
            attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, m_link.c_str());
            if (FAILED(MFCreateDeviceSource(attrs.Get(), &m_source)))
                return false;

            // "معالجة متقدمة" = القارئ بيفك MJPG وبيحوّل الألوان لحالو
            ComPtr<IMFAttributes> ra;
            MFCreateAttributes(&ra, 1);
            ra->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
            if (FAILED(MFCreateSourceReaderFromMediaSource(m_source.Get(), ra.Get(), &m_reader)))
                return false;

            // منختار أحسن وضع: لحد 1080p و 30 إطار أو أكتر
            const DWORD stream = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
            ComPtr<IMFMediaType> best;
            double bestScore = -1;
            for (DWORD i = 0;; ++i)
            {
                ComPtr<IMFMediaType> t;
                if (FAILED(m_reader->GetNativeMediaType(stream, i, &t)))
                    break;
                UINT32 w = 0, h = 0, n = 0, d = 1;
                MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
                MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &n, &d);
                const double fps = d ? (double)n / d : 0;
                double score = 0;
                if (h <= 1080) score += (double)w * h;
                if (fps >= 29) score += 1e8;
                if (fps > 61)  score -= 1e7;           // 120 هرتز ما منحتاجه
                if (score > bestScore)
                {
                    bestScore = score;
                    best = t;
                }
            }
            if (best)
                m_reader->SetCurrentMediaType(stream, nullptr, best.Get());

            ComPtr<IMFMediaType> out;
            MFCreateMediaType(&out);
            out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (FAILED(m_reader->SetCurrentMediaType(stream, nullptr, out.Get())))
                return false;
            ReadSize();
            return true;
        }

        void ReadSize()
        {
            ComPtr<IMFMediaType> cur;
            if (SUCCEEDED(m_reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur)))
            {
                UINT32 w = 0, h = 0;
                MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &w, &h);
                m_w = (int)w;
                m_h = (int)h;
                m_stride = (LONG)MFGetAttributeUINT32(cur.Get(), MF_MT_DEFAULT_STRIDE, (UINT32)(w * 4));
            }
        }

        void Run()
        {
            if (!Open())
            {
                m_failed = true;
                return;
            }
            while (m_running)
            {
                DWORD index = 0, flags = 0;
                LONGLONG ts = 0;
                ComPtr<IMFSample> sample;
                const HRESULT hr = m_reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &index, &flags, &ts, &sample);
                if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)))
                {
                    m_failed = true;          // الكاميرا انفصلت
                    return;
                }
                if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                    ReadSize();
                if (!sample || m_w <= 0 || m_h <= 0)
                    continue;

                ComPtr<IMFMediaBuffer> buffer;
                if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
                    continue;

                auto frame = std::make_shared<MediaFrame>();
                frame->width  = m_w;
                frame->height = m_h;
                frame->pixels.resize((size_t)m_w * m_h * 4);

                BYTE* scan0 = nullptr;
                LONG  pitch = 0;
                ComPtr<IMF2DBuffer> buf2d;
                bool locked2d = false;
                BYTE* raw = nullptr;
                if (SUCCEEDED(buffer.As(&buf2d)) && SUCCEEDED(buf2d->Lock2D(&scan0, &pitch)))
                    locked2d = true;
                else
                {
                    DWORD len = 0;
                    if (FAILED(buffer->Lock(&raw, nullptr, &len)))
                        continue;
                    pitch = m_stride;
                    scan0 = pitch < 0 ? raw + (size_t)(-pitch) * (m_h - 1) : raw;   // من تحت لفوق
                }

                for (int y = 0; y < m_h; ++y)
                {
                    const BYTE* src = scan0 + (ptrdiff_t)y * pitch;
                    BYTE* dst = frame->pixels.data() + (size_t)y * m_w * 4;
                    memcpy(dst, src, (size_t)m_w * 4);
                    for (int x = 0; x < m_w; ++x)
                        dst[x * 4 + 3] = 255;           // RGB32 ما فيه شفافية
                }

                if (locked2d) buf2d->Unlock2D();
                else          buffer->Unlock();

                frame->version = g_version++;
                std::lock_guard<std::mutex> lock(m_mutex);
                m_frame = frame;
            }
        }

        std::wstring              m_link;
        std::thread               m_thread;
        std::atomic<bool>         m_running{ false };
        std::atomic<bool>         m_failed{ false };
        ULONGLONG                 m_created = 0;
        std::mutex                m_mutex;
        std::shared_ptr<const MediaFrame> m_frame;

        ComPtr<IMFMediaSource>    m_source;
        ComPtr<IMFSourceReader>   m_reader;
        int                       m_w = 0, m_h = 0;
        LONG                      m_stride = 0;
    };

    std::mutex g_camMutex;
    std::map<std::wstring, std::shared_ptr<CameraDevice>> g_cams;

    std::shared_ptr<const MediaFrame> CameraFrame(const std::wstring& link, bool* missing)
    {
        std::vector<std::shared_ptr<CameraDevice>> closing;     // منسكّرهم برّا القفل (بياخدوا وقت)
        std::shared_ptr<CameraDevice> cam;
        {
            std::lock_guard<std::mutex> lock(g_camMutex);
            const ULONGLONG now = Now();

            auto& slot = g_cams[link];
            // انفصلت؟ منجرّب نفتحها من جديد كل 3 ثواني
            if (slot && slot->Failed() && now - slot->Created() > 3000)
            {
                closing.push_back(slot);
                slot.reset();
            }
            if (!slot)
                slot = std::make_shared<CameraDevice>(link);
            slot->m_lastUsed = now;
            cam = slot;

            // الكاميرات اللي ما حدا رسمها من 5 ثواني منسكّرها (بيطفى ضو الكاميرا)
            for (auto it = g_cams.begin(); it != g_cams.end();)
            {
                if (it->second && now - it->second->m_lastUsed > 5000)
                {
                    closing.push_back(it->second);
                    it = g_cams.erase(it);
                }
                else
                    ++it;
            }
        }
        if (missing)
            *missing = cam->Failed();
        return cam->Failed() ? nullptr : cam->Latest();
    }

    // =========================================================
    //  الصور
    // =========================================================
    struct ImageEntry
    {
        std::shared_ptr<const MediaFrame> frame;
        FILETIME  written{};
        ULONGLONG checked = 0;
    };
    std::mutex g_imgMutex;
    std::map<std::wstring, ImageEntry> g_images;

    ComPtr<IWICImagingFactory> Wic()
    {
        ComPtr<IWICImagingFactory> f;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        return f;
    }

    std::shared_ptr<const MediaFrame> LoadImageFile(const std::wstring& path)
    {
        ComPtr<IWICImagingFactory> wic = Wic();
        if (!wic)
            return nullptr;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> source;
        if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) ||
            FAILED(decoder->GetFrame(0, &source)))
            return nullptr;

        UINT w = 0, h = 0;
        source->GetSize(&w, &h);
        if (w == 0 || h == 0)
            return nullptr;

        // صور ضخمة: منصغّرها لحد 4096
        ComPtr<IWICBitmapSource> input = source;
        const UINT maxSide = 4096;
        if (w > maxSide || h > maxSide)
        {
            const double k = (double)maxSide / std::max(w, h);
            const UINT nw = std::max(1u, (UINT)(w * k)), nh = std::max(1u, (UINT)(h * k));
            ComPtr<IWICBitmapScaler> scaler;
            if (SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
                SUCCEEDED(scaler->Initialize(source.Get(), nw, nh, WICBitmapInterpolationModeFant)))
            {
                input = scaler;
                w = nw;
                h = nh;
            }
        }

        ComPtr<IWICFormatConverter> conv;
        if (FAILED(wic->CreateFormatConverter(&conv)) ||
            FAILED(conv->Initialize(input.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                    WICBitmapPaletteTypeMedianCut)))
            return nullptr;

        auto frame = std::make_shared<MediaFrame>();
        frame->width  = (int)w;
        frame->height = (int)h;
        frame->pixels.resize((size_t)w * h * 4);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)frame->pixels.size(), frame->pixels.data())))
            return nullptr;
        frame->version = g_version++;
        return frame;
    }

    std::shared_ptr<const MediaFrame> ImageFrame(const std::wstring& path, bool* missing)
    {
        std::lock_guard<std::mutex> lock(g_imgMutex);
        ImageEntry& e = g_images[path];
        const ULONGLONG now = Now();
        // كل ثانيتين منشوف إذا الملف تغيّر (عدّلت الصورة؟ منحدّثها)
        if (!e.frame || now - e.checked > 2000)
        {
            e.checked = now;
            WIN32_FILE_ATTRIBUTE_DATA fa{};
            if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa))
            {
                e.frame.reset();
            }
            else if (!e.frame || CompareFileTime(&fa.ftLastWriteTime, &e.written) != 0)
            {
                e.written = fa.ftLastWriteTime;
                e.frame = LoadImageFile(path);
            }
        }
        if (missing)
            *missing = !e.frame;
        return e.frame;
    }

    // =========================================================
    //  النص
    // =========================================================
    std::mutex g_textMutex;
    std::map<std::wstring, std::shared_ptr<const MediaFrame>> g_texts;
    ComPtr<ID2D1Factory>   g_textD2d;
    ComPtr<IDWriteFactory> g_textDw;

    bool HasArabic(const std::wstring& s)
    {
        for (wchar_t c : s)
            if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) || (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF))
                return true;
        return false;
    }

    std::shared_ptr<const MediaFrame> RenderText(const SceneLayer& l)
    {
        if (!g_textD2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, g_textD2d.GetAddressOf())))
            return nullptr;
        if (!g_textDw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                    reinterpret_cast<IUnknown**>(g_textDw.GetAddressOf()))))
            return nullptr;
        ComPtr<IWICImagingFactory> wic = Wic();
        if (!wic)
            return nullptr;

        const std::wstring text = l.source.empty() ? L" " : l.source;
        const float size = 96.0f;
        const bool  rtl  = HasArabic(text);

        ComPtr<IDWriteTextFormat> format;
        if (FAILED(g_textDw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, size, rtl ? L"ar" : L"en-us", &format)))
            return nullptr;
        format->SetReadingDirection(rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(g_textDw->CreateTextLayout(text.c_str(), (UINT32)text.size(), format.Get(), 10000, 2000, &layout)))
            return nullptr;
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        layout->SetMaxWidth(std::max(1.0f, m.widthIncludingTrailingWhitespace));
        layout->GetMetrics(&m);

        const float padX = l.background ? size * 0.40f : size * 0.10f;
        const float padY = l.background ? size * 0.18f : size * 0.08f;
        const UINT W = (UINT)std::ceil(m.widthIncludingTrailingWhitespace + padX * 2);
        const UINT H = (UINT)std::ceil(m.height + padY * 2);

        ComPtr<IWICBitmap> bitmap;
        if (FAILED(wic->CreateBitmap(W, H, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)))
            return nullptr;
        ComPtr<ID2D1RenderTarget> rt;
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (FAILED(g_textD2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt)))
            return nullptr;
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        const float a = ((l.color >> 24) & 0xFF) / 255.0f;
        const D2D1_COLOR_F color = D2D1::ColorF(((l.color >> 16) & 0xFF) / 255.0f, ((l.color >> 8) & 0xFF) / 255.0f,
                                                (l.color & 0xFF) / 255.0f, a > 0 ? a : 1.0f);
        ComPtr<ID2D1SolidColorBrush> brush;
        rt->CreateSolidColorBrush(color, &brush);

        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        if (l.background)
        {
            ComPtr<ID2D1SolidColorBrush> bg;
            rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.62f), &bg);
            const float r = H * 0.22f;
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, (float)W, (float)H), r, r), bg.Get());
        }
        else
        {
            // ظل خفيف لحتى ينقرا فوق أي خلفية
            ComPtr<ID2D1SolidColorBrush> shadow;
            rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.55f), &shadow);
            rt->DrawTextLayout(D2D1::Point2F(padX + 3, padY + 4), layout.Get(), shadow.Get());
        }
        rt->DrawTextLayout(D2D1::Point2F(padX, padY), layout.Get(), brush.Get());
        if (FAILED(rt->EndDraw()))
            return nullptr;

        auto frame = std::make_shared<MediaFrame>();
        frame->width  = (int)W;
        frame->height = (int)H;
        frame->pixels.resize((size_t)W * H * 4);
        if (FAILED(bitmap->CopyPixels(nullptr, W * 4, (UINT)frame->pixels.size(), frame->pixels.data())))
            return nullptr;
        frame->version = g_version++;
        return frame;
    }

    std::shared_ptr<const MediaFrame> TextFrame(const SceneLayer& l)
    {
        const std::wstring key = l.source + L"\x1F" + std::to_wstring(l.color) + (l.background ? L"\x1F" L"1" : L"\x1F" L"0");
        std::lock_guard<std::mutex> lock(g_textMutex);
        auto it = g_texts.find(key);
        if (it != g_texts.end())
            return it->second;
        if (g_texts.size() > 64)
            g_texts.clear();
        auto frame = RenderText(l);
        g_texts[key] = frame;
        return frame;
    }
}

// =============================================================
std::vector<CameraInfo> ListCameras()
{
    std::vector<CameraInfo> list;
    ComPtr<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(&attrs, 1)))
        return list;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attrs.Get(), &devices, &count)))
        return list;
    for (UINT32 i = 0; i < count; ++i)
    {
        CameraInfo info;
        WCHAR* s = nullptr;
        UINT32 len = 0;
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &s, &len)))
        {
            info.name = s;
            CoTaskMemFree(s);
        }
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &s, &len)))
        {
            info.link = s;
            CoTaskMemFree(s);
        }
        devices[i]->Release();
        if (!info.link.empty())
            list.push_back(info);
    }
    CoTaskMemFree(devices);
    return list;
}

std::shared_ptr<const MediaFrame> GetLayerFrame(const SceneLayer& layer, bool* missing)
{
    if (missing)
        *missing = false;
    switch (layer.kind)
    {
    case LayerKind::Camera: return CameraFrame(layer.source, missing);
    case LayerKind::Image:  return ImageFrame(layer.source, missing);
    case LayerKind::Text:   return TextFrame(layer);
    default:                return nullptr;
    }
}

bool MediaNaturalSize(const SceneLayer& layer, int& width, int& height)
{
    std::shared_ptr<const MediaFrame> f = GetLayerFrame(layer);
    if (layer.kind == LayerKind::Camera && !f)
    {
        // الكاميرا لسا عم تفتح: منستنى شوي لأول صورة
        for (int i = 0; i < 30 && !f; ++i)
        {
            Sleep(50);
            f = GetLayerFrame(layer);
        }
        if (!f)
        {
            width  = 1280;
            height = 720;
            return true;
        }
    }
    if (!f)
        return false;
    width  = f->width;
    height = f->height;
    return true;
}
