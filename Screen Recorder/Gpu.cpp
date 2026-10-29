// =============================================================
//  Gpu.cpp - التصوير والتركيب على كرت الشاشة
// =============================================================
#include "Gpu.h"
#include "Media.h"

#include <unknwn.h>
#include <d3d11.h>
#include <d3d10.h>      // ID3D10Multithread
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <wrl/client.h>

// C++/WinRT: مكتبة ويندوز الحديثة (فيها Windows Graphics Capture)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
#include <atomic>
#include <map>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowsapp.lib")

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wd3 = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace
{
    // من صورة WinRT لـ texture عادي تبع Direct3D
    ComPtr<ID3D11Texture2D> TextureOf(const wd3::IDirect3DSurface& surface)
    {
        auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> tex;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&tex)));
        return tex;
    }

    // مصدر واحد (شاشة أو نافذة) مع جلسة التصوير تبعه
    struct Source
    {
        CaptureTarget                   target;
        wgc::GraphicsCaptureItem        item{ nullptr };
        wgc::Direct3D11CaptureFramePool pool{ nullptr };
        wgc::GraphicsCaptureSession     session{ nullptr };
        winrt::Windows::Graphics::SizeInt32 poolSize{};
        winrt::event_token              closedToken{};
        std::shared_ptr<std::atomic<bool>> closed = std::make_shared<std::atomic<bool>>(false);

        // آخر صورة وصلتنا (ويندوز ما بيبعت صورة جديدة إلا إذا تغيّر شي)
        ComPtr<ID3D11Texture2D> cache;
        ComPtr<ID2D1Bitmap1>    bitmap;
        UINT                    cacheW = 0, cacheH = 0;
        bool                    failed = false;

        ~Source()
        {
            try
            {
                if (item)    item.Closed(closedToken);
                if (session) session.Close();
                if (pool)    pool.Close();
            }
            catch (...) {}
        }
    };
}

struct GpuCompositor::Impl
{
    bool captureCursor = true;

    ComPtr<ID3D11Device>        device;
    ComPtr<ID3D11DeviceContext> context;
    wd3::IDirect3DDevice        winrtDevice{ nullptr };

    ComPtr<ID2D1Factory1>       d2dFactory;
    ComPtr<ID2D1Device>         d2dDevice;
    ComPtr<ID2D1DeviceContext>  dc;

    // اللوحة (على كرت الشاشة) + نسخة بنقدر نقراها من المعالج
    ComPtr<ID3D11Texture2D>     canvas;
    ComPtr<ID3D11Texture2D>     staging;
    ComPtr<ID2D1Bitmap1>        canvasTarget;
    int                         canvasW = 0, canvasH = 0;

    std::map<int, std::unique_ptr<Source>> sources;    // لكل طبقة مصدر

    // صور الكاميرا/الصورة/النص على كرت الشاشة
    struct MediaBitmap { unsigned long long version = 0; ComPtr<ID2D1Bitmap1> bitmap; int w = 0, h = 0; };
    std::map<int, MediaBitmap> media;

    // الانتقال: نسخة مجمّدة من آخر صورة للمشهد القديم
    unsigned                fadeId = 0;
    bool                    haveFrame = false;
    ComPtr<ID3D11Texture2D> frozen;
    ComPtr<ID2D1Bitmap1>    frozenBitmap;
    bool                    frozenValid = false;

    bool Init()
    {
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
        // VIDEO_SUPPORT: لحتى نقدر نحوّل الألوان ونضغط على نفس الجهاز (التسجيل السريع)
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                       levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr, &context);
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr, &context);
        if (FAILED(hr))
            return false;
        // المشفّر بيستعمل نفس الجهاز من خيط تاني: منحميه
        ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(device.As(&mt)))
            mt->SetMultithreadProtected(TRUE);

        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(device.As(&dxgi)))
            return false;

        // نسخة WinRT من نفس الجهاز (بدها ياها Windows Graphics Capture)
        winrt::com_ptr<::IInspectable> inspectable;
        if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put())))
            return false;
        winrtDevice = inspectable.as<wd3::IDirect3DDevice>();

        // Direct2D فوق نفس الجهاز
        D2D1_FACTORY_OPTIONS opts{};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts, &d2dFactory)))
            return false;
        if (FAILED(d2dFactory->CreateDevice(dxgi.Get(), &d2dDevice)))
            return false;
        if (FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)))
            return false;
        dc->SetDpi(96, 96);                 // وحدة الرسم = بكسل
        return true;
    }

    bool EnsureCanvas(int w, int h)
    {
        if (canvas && w == canvasW && h == canvasH)
            return true;
        canvas.Reset();
        staging.Reset();
        canvasTarget.Reset();
        dc->SetTarget(nullptr);

        D3D11_TEXTURE2D_DESC d{};
        d.Width            = w;
        d.Height           = h;
        d.MipLevels        = 1;
        d.ArraySize        = 1;
        d.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage            = D3D11_USAGE_DEFAULT;
        d.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&d, nullptr, &canvas)))
            return false;

        d.Usage          = D3D11_USAGE_STAGING;
        d.BindFlags      = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&d, nullptr, &staging)))
            return false;

        ComPtr<IDXGISurface> surface;
        canvas.As(&surface);
        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &canvasTarget)))
            return false;

        canvasW = w;
        canvasH = h;
        haveFrame = false;
        frozen.Reset();
        frozenBitmap.Reset();
        frozenValid = false;
        return true;
    }

    // منجمّد الصورة الحالية (قبل ما نرسم المشهد الجديد)
    void Freeze()
    {
        frozenValid = false;
        if (!frozen)
        {
            D3D11_TEXTURE2D_DESC d{};
            canvas->GetDesc(&d);
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.CPUAccessFlags = 0;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &frozen)))
                return;
            ComPtr<IDXGISurface> surface;
            frozen.As(&surface);
            const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &frozenBitmap)))
            {
                frozen.Reset();
                return;
            }
        }
        context->CopyResource(frozen.Get(), canvas.Get());
        frozenValid = true;
    }

    // رسم الكاميرا/الصورة/النص
    void DrawMedia(const SceneLayer& l, const D2D1_RECT_F& dst, std::vector<int>* lostIds)
    {
    if (!l.visible)        // مخفية: ما منفتح الكاميرا أصلاً (بينطفى ضوّها)
        return;
        bool missing = false;
        std::shared_ptr<const MediaFrame> f = GetLayerFrame(l, &missing);
        if (missing && lostIds)
            lostIds->push_back(l.id);
        if (!l.visible || !f || f->width <= 0 || f->height <= 0)
            return;

        MediaBitmap& m = media[l.id];
        if (!m.bitmap || m.version != f->version)
        {
            if (!m.bitmap || m.w != f->width || m.h != f->height)
            {
                m.bitmap.Reset();
                const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                if (FAILED(dc->CreateBitmap(D2D1::SizeU(f->width, f->height), f->pixels.data(), f->width * 4, &props, &m.bitmap)))
                    return;
                m.w = f->width;
                m.h = f->height;
            }
            else
                m.bitmap->CopyFromMemory(nullptr, f->pixels.data(), f->width * 4);
            m.version = f->version;
        }

        const float dw = dst.right - dst.left, dh = dst.bottom - dst.top;
        if (dw <= 0 || dh <= 0)
            return;

        // الجزء اللي منرسمه من الصورة: أول شي القص (Alt + سحب)،
        // وبعدين الكاميرا منقصها لتملا المكان بدون مط
        float sx = (float)(m.w * l.cropL), sy = (float)(m.h * l.cropT);
        float sw = std::max(1.0f, (float)(m.w * (1 - l.cropL - l.cropR)));
        float sh = std::max(1.0f, (float)(m.h * (1 - l.cropT - l.cropB)));
        if (l.kind == LayerKind::Camera)
        {
            const float dstAspect = dw / dh, srcAspect = sw / sh;
            if (srcAspect > dstAspect) { const float nw = sh * dstAspect; sx += (sw - nw) / 2; sw = nw; }
            else                       { const float nh = sw / dstAspect; sy += (sh - nh) / 2; sh = nh; }
        }

        const bool mirror = l.kind == LayerKind::Camera && l.mirror;
        if (l.shape == SHAPE_RECT && !mirror)
        {
            const D2D1_RECT_F src = D2D1::RectF(sx, sy, sx + sw, sy + sh);
            const float scale = dw / sw;
            dc->DrawBitmap(m.bitmap.Get(), &dst, 1.0f,
                           scale < 0.75f ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_LINEAR,
                           &src, nullptr);
            return;
        }

        // شكل (دائرة / زوايا مدوّرة) أو مراية: منرسم بفرشاة من الصورة
        ComPtr<ID2D1BitmapBrush1> brush;
        if (FAILED(dc->CreateBitmapBrush(m.bitmap.Get(), &brush)))
            return;
        brush->SetInterpolationMode1(D2D1_INTERPOLATION_MODE_LINEAR);
        const float kx = dw / sw, ky = dh / sh;
        D2D1::Matrix3x2F t = D2D1::Matrix3x2F::Translation(-sx, -sy) * D2D1::Matrix3x2F::Scale(kx, ky);
        if (mirror)
            t = t * D2D1::Matrix3x2F::Scale(-1, 1) * D2D1::Matrix3x2F::Translation(dw, 0);
        t = t * D2D1::Matrix3x2F::Translation(dst.left, dst.top);
        brush->SetTransform(t);

        if (l.shape == SHAPE_CIRCLE)
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F((dst.left + dst.right) / 2, (dst.top + dst.bottom) / 2), dw / 2, dh / 2), brush.Get());
        else if (l.shape == SHAPE_ROUNDED)
        {
            const float r = std::min(dw, dh) * 0.12f;
            dc->FillRoundedRectangle(D2D1::RoundedRect(dst, r, r), brush.Get());
        }
        else
            dc->FillRectangle(dst, brush.Get());
    }

    // منفتح جلسة تصوير لمصدر جديد
    std::unique_ptr<Source> OpenSource(const CaptureTarget& t)
    {
        auto src = std::make_unique<Source>();
        src->target = t;
        try
        {
            auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            if (t.isMonitor)
            {
                HMONITOR monitor = MonitorFromRect(&t.rect, MONITOR_DEFAULTTONEAREST);
                winrt::check_hresult(interop->CreateForMonitor(monitor, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                               winrt::put_abi(src->item)));
            }
            else
            {
                winrt::check_hresult(interop->CreateForWindow(t.hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                              winrt::put_abi(src->item)));
            }

            // إذا النافذة انسكّرت ويندوز بيخبّرنا
            auto closed = src->closed;
            src->closedToken = src->item.Closed([closed](auto&&, auto&&) { *closed = true; });

            src->poolSize = src->item.Size();
            src->pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, src->poolSize);   // 3 = ما بيضيع إطار
            src->session = src->pool.CreateCaptureSession(src->item);
            src->session.IsCursorCaptureEnabled(captureCursor);
            try { src->session.IsBorderRequired(false); } catch (...) {}   // بلا الإطار الأصفر (ويندوز 11)
            src->session.StartCapture();
        }
        catch (...)
        {
            src->failed = true;
        }
        return src;
    }

    // منسحب آخر صورة وصلت من المصدر لـ cache
    // بيرجّع true إذا وصلت صورة جديدة من ويندوز
    bool Update(Source& s)
    {
        if (s.failed || !s.pool)
            return false;
        wgc::Direct3D11CaptureFrame latest{ nullptr };
        for (auto f = s.pool.TryGetNextFrame(); f; f = s.pool.TryGetNextFrame())
        {
            if (latest) latest.Close();
            latest = f;                      // منحتفظ بالأحدث بس
        }
        if (!latest)
            return false;

        const auto size = latest.ContentSize();
        ComPtr<ID3D11Texture2D> tex = TextureOf(latest.Surface());
        D3D11_TEXTURE2D_DESC td{};
        tex->GetDesc(&td);

        // إذا تغيّر حجم النافذة منجدّد "خزّان" الصور
        if (size.Width != s.poolSize.Width || size.Height != s.poolSize.Height)
        {
            s.poolSize = size;
            s.pool.Recreate(winrtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, size);
        }

        const UINT w = std::min<UINT>((UINT)std::max(1, size.Width), td.Width);
        const UINT h = std::min<UINT>((UINT)std::max(1, size.Height), td.Height);
        if (!s.cache || w != s.cacheW || h != s.cacheH)
        {
            s.cache.Reset();
            s.bitmap.Reset();
            D3D11_TEXTURE2D_DESC d{};
            d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &s.cache)))
            {
                latest.Close();
                return false;
            }
            s.cacheW = w;
            s.cacheH = h;
        }
        const D3D11_BOX box{ 0, 0, 0, w, h, 1 };
        context->CopySubresourceRegion(s.cache.Get(), 0, 0, 0, 0, tex.Get(), 0, &box);
        latest.Close();
        return true;
    }

    // الإطارات المتغيّرة: في شي جديد من ويندوز (أو الكاميرا) من آخر مرة؟
    // بتنسخ الصورة الجديدة لمكانها (متل Compose) بس بدون ما ترسم المشهد
    std::map<int, unsigned long long> polledMedia;
    bool PollNewFrames(const std::vector<SceneLayer>& layers)
    {
        bool fresh = false;
        for (const SceneLayer& l : layers)
        {
            if (!l.visible)
                continue;
            if (l.kind == LayerKind::Capture)
            {
                auto it = sources.find(l.id);
                if (it == sources.end() || !it->second)
                    fresh = true;                  // لسا ما انفتح: Compose بيفتحه
                else if (Update(*it->second))
                    fresh = true;
            }
            else
            {
                std::shared_ptr<const MediaFrame> f = GetLayerFrame(l, nullptr);
                if (f && f->version != polledMedia[l.id])
                {
                    polledMedia[l.id] = f->version;
                    fresh = true;
                }
            }
        }
        return fresh;
    }

    bool Compose(const std::vector<SceneLayer>& layers, int cw, int ch, const ComposeParams& p)
    {
        const int outW = p.outW, outH = p.outH;
        std::vector<int>* lostIds = p.lostIds;
        if (!EnsureCanvas(outW, outH))
            return false;

        // منسكّر مصادر الطبقات اللي انحذفت
        for (auto it = sources.begin(); it != sources.end();)
        {
            bool exists = false;
            for (const SceneLayer& l : layers)
                if (l.id == it->first && l.kind == LayerKind::Capture) { exists = true; break; }
            it = exists ? std::next(it) : sources.erase(it);
        }
        for (auto it = media.begin(); it != media.end();)
        {
            bool exists = false;
            for (const SceneLayer& l : layers)
                if (l.id == it->first && l.kind != LayerKind::Capture) { exists = true; break; }
            it = exists ? std::next(it) : media.erase(it);
        }

        // تبديل مشهد: منجمّد آخر صورة من القديم لحتى نعمل تلاشي منها
        if (p.fade.id != fadeId)
        {
            fadeId = p.fade.id;
            if (haveFrame && p.fade.progress < 1)
                Freeze();
            else
                frozenValid = false;
        }
        if (p.fade.progress >= 1)
            frozenValid = false;
        const bool fading = frozenValid && p.fade.progress < 1;

        dc->SetTarget(canvasTarget.Get());
        dc->BeginDraw();
        dc->Clear(D2D1::ColorF(0, 0, 0, 1));

        if (fading)
        {
            dc->DrawBitmap(frozenBitmap.Get(), nullptr, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
            dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                 D2D1::IdentityMatrix(), p.fade.progress), nullptr);
            ComPtr<ID2D1SolidColorBrush> black;
            dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &black);
            dc->FillRectangle(D2D1::RectF(0, 0, (float)outW, (float)outH), black.Get());
        }

        // أي جزء من اللوحة منرسم
        const double vx = p.viewW > 0 ? p.viewX : 0, vy = p.viewW > 0 ? p.viewY : 0;
        const double vw = p.viewW > 0 ? p.viewW : cw, vh = p.viewH > 0 ? p.viewH : ch;
        const float sx = (float)(outW / vw), sy = (float)(outH / vh);
        for (const SceneLayer& l : layers)
        {
            const D2D1_RECT_F dst = D2D1::RectF((float)(l.x - vx) * sx, (float)(l.y - vy) * sy,
                                                (float)(l.x - vx + l.w) * sx, (float)(l.y - vy + l.h) * sy);
            if (l.kind != LayerKind::Capture)
            {
                DrawMedia(l, dst, lostIds);
                continue;
            }

            auto& src = sources[l.id];
            bool fresh = false;
            if (!src || !SameTarget(src->target, l.target))    // جديد، أو النافذة رجعت انفتحت
            {
                src = OpenSource(l.target);
                fresh = true;
            }

            const bool lost = *src->closed || (!l.target.isMonitor && !IsWindow(l.target.hwnd));
            if (lost)
            {
                if (lostIds) lostIds->push_back(l.id);
                continue;
            }
            Update(*src);
            // مصدر جديد: منستنى شوي لأول صورة (بدل ما يطلع أسود لحظة)
            for (int i = 0; fresh && !src->cache && !src->failed && i < 15; ++i)
            {
                Sleep(10);
                Update(*src);
            }
            if (!l.visible || !src->cache)
                continue;

            if (!src->bitmap)
            {
                ComPtr<IDXGISurface> surface;
                src->cache.As(&surface);
                const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
                if (FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &src->bitmap)))
                    continue;
            }

            // القص: الجزء الباقي من المصدر بس
            const D2D1_RECT_F part = D2D1::RectF((float)(src->cacheW * l.cropL), (float)(src->cacheH * l.cropT),
                                                 (float)(src->cacheW * (1 - l.cropR)), (float)(src->cacheH * (1 - l.cropB)));
            // تصغير كبير = فلتر أنعم (أبطأ شوي) ، غير هيك فلتر سريع
            const float scale = (dst.right - dst.left) / std::max(1.0f, part.right - part.left);
            dc->DrawBitmap(src->bitmap.Get(), &dst, 1.0f,
                           scale < 0.75f ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_LINEAR,
                           l.HasCrop() ? &part : nullptr, nullptr);
        }

        if (fading)
            dc->PopLayer();
        haveFrame = true;

        if (FAILED(dc->EndDraw()))
            return false;
        dc->SetTarget(nullptr);

        // الطريق السريع: الصورة بتضل على كرت الشاشة
        if (p.gpuOnly)
        {
            if (p.outTexture)
                *p.outTexture = canvas.Get();
            return true;
        }

        // منسحب الصورة الجاهزة للذاكرة
        context->CopyResource(staging.Get(), canvas.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)))
            return false;
        for (int y = 0; y < outH; ++y)
        {
            const BYTE* srcRow = (const BYTE*)map.pData + (size_t)y * map.RowPitch;
            BYTE*       dstRow = p.out + (size_t)(p.bottomUp ? (outH - 1 - y) : y) * p.stride;
            memcpy(dstRow, srcRow, (size_t)outW * 4);
        }
        context->Unmap(staging.Get(), 0);
        return true;
    }
};

// =============================================================
std::unique_ptr<GpuCompositor> GpuCompositor::Create(bool captureCursor)
{
    try
    {
        if (!wgc::GraphicsCaptureSession::IsSupported())
            return nullptr;
        std::unique_ptr<GpuCompositor> c(new GpuCompositor());
        c->m = std::make_unique<Impl>();
        c->m->captureCursor = captureCursor;
        if (!c->m->Init())
            return nullptr;
        return c;
    }
    catch (...)
    {
        return nullptr;
    }
}

GpuCompositor::~GpuCompositor() = default;

ID3D11Device* GpuCompositor::Device() const
{
    return m ? m->device.Get() : nullptr;
}

bool GpuCompositor::PollNewFrames(const std::vector<SceneLayer>& layers)
{
    try
    {
        return m->PollNewFrames(layers);
    }
    catch (...)
    {
        return true;
    }
}

bool GpuCompositor::Compose(const std::vector<SceneLayer>& layers, int canvasW, int canvasH, const ComposeParams& params)
{
    try
    {
        return m->Compose(layers, canvasW, canvasH, params);
    }
    catch (...)
    {
        return false;
    }
}
