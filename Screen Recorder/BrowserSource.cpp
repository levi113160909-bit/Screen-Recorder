// =============================================================
//  BrowserSource.cpp - شوف BrowserSource.h
// =============================================================
#include "BrowserSource.h"

#include <unknwn.h>
#include <wrl.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shlobj.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include "third_party/webview2/WebView2.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "CoreMessaging.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "third_party/webview2/WebView2LoaderStatic.lib")

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace wuc = winrt::Windows::UI::Composition;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wd3 = winrt::Windows::Graphics::DirectX::Direct3D11;
namespace wsy = winrt::Windows::System;

namespace
{
    constexpr ULONGLONG IDLE_CLOSE_MS = 30'000;    // صفحة ما حدا طلبها من 30 ثانية: منسكّرها
    constexpr ULONGLONG FRAME_MS      = 33;        // 30 صورة بالثانية بتكفي للتنبيهات والشات
    constexpr ULONGLONG RETRY_MS      = 10'000;    // فشل التحميل: منعيد بعد 10 ثواني

    // CSS الافتراضي (نفس اللي بيحطّه OBS): خلفية شفافة وبلا شريط تمرير
    // بلا !important لحتى الموقع اللي عنده خلفية يضل متل ما هو
    const wchar_t* DEFAULT_CSS_SCRIPT =
        L"(function(){function add(){if(document.getElementById('__sr_css'))return;"
        L"var s=document.createElement('style');s.id='__sr_css';"
        L"s.textContent='body{background-color:rgba(0,0,0,0);margin:0px auto;overflow:hidden;}';"
        L"var h=document.head||document.documentElement;if(h)h.insertBefore(s,h.firstChild);}"
        L"if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',add);else add();})();";

    std::atomic<uint64_t> g_version{ 1ull << 40 };   // مجال منفصل عن باقي المصادر

    // ---------------------------------------------------------
    //  اللي بيتشارك بين خيط المتصفح وخيوط الرسم
    // ---------------------------------------------------------
    struct Shared
    {
        std::mutex                        mutex;
        std::shared_ptr<const MediaFrame> frame;
        std::atomic<bool>                 failed{ false };
        std::atomic<ULONGLONG>            lastUsed{ 0 };
    };

    std::mutex                                     g_mutex;      // g_shared + g_queue
    std::condition_variable                        g_cv;
    std::map<std::wstring, std::shared_ptr<Shared>> g_shared;
    wsy::DispatcherQueue                           g_queue{ nullptr };
    std::atomic<bool>                              g_threadStarted{ false }, g_threadFailed{ false };

    // ---------------------------------------------------------
    //  خيط المتصفح بس
    // ---------------------------------------------------------
    struct Page
    {
        std::wstring key, url;
        int          w = 0, h = 0;
        std::shared_ptr<Shared> sh;
        bool         closed = false;

        ComPtr<ICoreWebView2CompositionController> comp;
        ComPtr<ICoreWebView2Controller>            ctrl;
        ComPtr<ICoreWebView2>                      wv;

        wuc::ContainerVisual               root{ nullptr }, holder{ nullptr };
        wgc::GraphicsCaptureItem           item{ nullptr };
        wgc::Direct3D11CaptureFramePool    pool{ nullptr };
        wgc::GraphicsCaptureSession        session{ nullptr };
        wsy::DispatcherQueueTimer          flush{ nullptr };   // صورة مأجّلة (لحتى ما تضيع آخر صورة)
        wgc::Direct3D11CaptureFrame        pending{ nullptr };

        ComPtr<ID3D11Texture2D> staging;
        ULONGLONG lastFrame = 0, retryAt = 0;
    };

    std::map<std::wstring, std::shared_ptr<Page>> t_pages;
    ComPtr<ICoreWebView2Environment>              t_env;
    bool                                          t_envPending = false, t_envFailed = false;
    std::vector<std::function<void()>>            t_waitingEnv;
    HWND                                          t_msgWnd = nullptr;
    wuc::Compositor                               t_comp{ nullptr };
    ComPtr<ID3D11Device>                          t_dev;
    ComPtr<ID3D11DeviceContext>                   t_ctx;
    wd3::IDirect3DDevice                          t_wdev{ nullptr };
    wsy::DispatcherQueueTimer                     t_timer{ nullptr };
    std::map<HWND, ComPtr<ICoreWebView2Controller>> t_windows;   // نوافذ التفاعل

    std::wstring Key(const std::wstring& url, int w, int h)
    {
        return url + L"\n" + std::to_wstring(w) + L"x" + std::to_wstring(h);
    }

    std::wstring UserDataFolder()
    {
        PWSTR appData = nullptr;
        std::wstring path;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
        {
            path = std::wstring(appData) + L"\\ScreenRecorder";
            CoTaskMemFree(appData);
            CreateDirectoryW(path.c_str(), nullptr);
            path += L"\\WebView2";
            CreateDirectoryW(path.c_str(), nullptr);
        }
        return path;
    }

    ComPtr<ID3D11Texture2D> TextureOf(const wd3::IDirect3DSurface& surface)
    {
        auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> tex;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&tex)));
        return tex;
    }

    // ---------------------------------------------------------
    //  الصور
    // ---------------------------------------------------------
    void ProcessFrame(Page& p, wgc::Direct3D11CaptureFrame frame)
    {
        try
        {
            ComPtr<ID3D11Texture2D> tex = TextureOf(frame.Surface());
            D3D11_TEXTURE2D_DESC d{};
            tex->GetDesc(&d);
            const auto content = frame.ContentSize();

            D3D11_TEXTURE2D_DESC sd{};
            if (p.staging)
                p.staging->GetDesc(&sd);
            if (!p.staging || sd.Width != d.Width || sd.Height != d.Height || sd.Format != d.Format)
            {
                p.staging.Reset();
                D3D11_TEXTURE2D_DESC n = d;
                n.Usage = D3D11_USAGE_STAGING;
                n.BindFlags = 0;
                n.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                n.MiscFlags = 0;
                n.MipLevels = 1;
                n.ArraySize = 1;
                n.SampleDesc = { 1, 0 };
                if (FAILED(t_dev->CreateTexture2D(&n, nullptr, &p.staging)))
                {
                    frame.Close();
                    return;
                }
            }
            t_ctx->CopyResource(p.staging.Get(), tex.Get());
            frame.Close();

            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(t_ctx->Map(p.staging.Get(), 0, D3D11_MAP_READ, 0, &m)))
                return;
            auto out = std::make_shared<MediaFrame>();
            out->width  = p.w;
            out->height = p.h;
            out->pixels.assign((size_t)p.w * p.h * 4, 0);
            const int cw = std::min<int>({ p.w, (int)d.Width, content.Width });
            const int ch = std::min<int>({ p.h, (int)d.Height, content.Height });
            for (int y = 0; y < ch; ++y)
                memcpy(out->pixels.data() + (size_t)y * p.w * 4, (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)cw * 4);
            t_ctx->Unmap(p.staging.Get(), 0);
            out->version = g_version++;

            std::lock_guard<std::mutex> lock(p.sh->mutex);
            p.sh->frame = std::move(out);
        }
        catch (...)
        {
        }
    }

    void OnFrame(const std::weak_ptr<Page>& wp, const wgc::Direct3D11CaptureFramePool& pool)
    {
        wgc::Direct3D11CaptureFrame f{ nullptr };
        try { f = pool.TryGetNextFrame(); } catch (...) { return; }
        if (!f)
            return;
        auto p = wp.lock();
        if (!p || p->closed)
        {
            f.Close();
            return;
        }
        const ULONGLONG now = GetTickCount64();
        if (now - p->lastFrame < FRAME_MS)
        {
            // سريعة كتير: منأجّلها (منحتفظ بالأحدث بس) لحتى آخر حالة للصفحة ما تضيع
            if (p->pending) p->pending.Close();
            p->pending = f;
            if (p->flush && !p->flush.IsRunning())
            {
                p->flush.Interval(std::chrono::milliseconds(FRAME_MS - (now - p->lastFrame) + 1));
                p->flush.Start();
            }
            return;
        }
        if (p->pending) { p->pending.Close(); p->pending = nullptr; }
        p->lastFrame = now;
        ProcessFrame(*p, f);
    }

    void ClosePage(Page& p)
    {
        p.closed = true;
        try
        {
            if (p.flush) p.flush.Stop();
            if (p.pending) p.pending.Close();
            if (p.session) p.session.Close();
            if (p.pool) p.pool.Close();
        }
        catch (...) {}
        if (p.ctrl)
            p.ctrl->Close();
        p.wv.Reset();
        p.comp.Reset();
        p.ctrl.Reset();
        p.pending = nullptr;
        p.session = nullptr;
        p.pool = nullptr;
        p.item = nullptr;
        p.holder = nullptr;
        p.root = nullptr;
        p.flush = nullptr;
        p.staging.Reset();
    }

    void Fail(Page& p)
    {
        p.sh->failed = true;
    }

    // ---------------------------------------------------------
    //  المتصفح
    // ---------------------------------------------------------
    void WithEnv(std::function<void()> fn)
    {
        if (t_env || t_envFailed)
        {
            fn();
            return;
        }
        t_waitingEnv.push_back(std::move(fn));
        if (t_envPending)
            return;
        t_envPending = true;

        // الصوت يشتغل بلا نقرة (صوت التنبيه) ، والصفحة تضل شغّالة وهي "مخفية"
        SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
            L"--autoplay-policy=no-user-gesture-required --disable-background-timer-throttling "
            L"--disable-renderer-backgrounding --disable-backgrounding-occluded-windows "
            L"--disable-features=CalculateNativeWinOcclusion");

        auto finish = [](HRESULT r, ICoreWebView2Environment* env)
        {
            t_envPending = false;
            if (SUCCEEDED(r) && env)
                t_env = env;
            else
                t_envFailed = true;
            auto list = std::move(t_waitingEnv);
            t_waitingEnv.clear();
            for (auto& f : list)
                f();
        };
        const std::wstring folder = UserDataFolder();
        const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, folder.empty() ? nullptr : folder.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [finish](HRESULT r, ICoreWebView2Environment* env) -> HRESULT
                {
                    finish(r, env);
                    return S_OK;
                }).Get());
        if (FAILED(hr))
            finish(hr, nullptr);
    }

    void StartCapture(const std::shared_ptr<Page>& p)
    {
        std::weak_ptr<Page> wp = p;
        p->item = wgc::GraphicsCaptureItem::CreateFromVisual(p->root);
        p->pool = wgc::Direct3D11CaptureFramePool::Create(t_wdev, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                                                          { p->w, p->h });
        p->session = p->pool.CreateCaptureSession(p->item);
        try { p->session.IsCursorCaptureEnabled(false); } catch (...) {}
        try { p->session.IsBorderRequired(false); } catch (...) {}

        p->flush = g_queue.CreateTimer();
        p->flush.IsRepeating(false);
        p->flush.Tick([wp](auto&&, auto&&)
        {
            auto pp = wp.lock();
            if (!pp || pp->closed || !pp->pending)
                return;
            auto f = pp->pending;
            pp->pending = nullptr;
            pp->lastFrame = GetTickCount64();
            ProcessFrame(*pp, f);
        });
        p->pool.FrameArrived([wp](const wgc::Direct3D11CaptureFramePool& pool, auto&&) { OnFrame(wp, pool); });
        p->session.StartCapture();
    }

    void SetupPage(const std::shared_ptr<Page>& p, ICoreWebView2CompositionController* cc)
    {
        p->comp = cc;
        if (FAILED(cc->QueryInterface(IID_PPV_ARGS(&p->ctrl))))
        {
            Fail(*p);
            return;
        }
        ComPtr<ICoreWebView2Controller3> c3;
        if (SUCCEEDED(p->ctrl.As(&c3)))
        {
            c3->put_ShouldDetectMonitorScaleChanges(FALSE);
            c3->put_RasterizationScale(1.0);
            c3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
        }
        ComPtr<ICoreWebView2Controller2> c2;
        if (SUCCEEDED(p->ctrl.As(&c2)))
            c2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{ 0, 0, 0, 0 });   // شفاف
        p->ctrl->put_Bounds(RECT{ 0, 0, p->w, p->h });
        p->ctrl->put_IsVisible(TRUE);
        if (FAILED(cc->put_RootVisualTarget(static_cast<IUnknown*>(winrt::get_abi(p->holder)))) ||
            FAILED(p->ctrl->get_CoreWebView2(&p->wv)) || !p->wv)
        {
            Fail(*p);
            return;
        }

        ComPtr<ICoreWebView2Settings> s;
        if (SUCCEEDED(p->wv->get_Settings(&s)))
        {
            s->put_AreDefaultContextMenusEnabled(FALSE);
            s->put_IsStatusBarEnabled(FALSE);
            s->put_IsZoomControlEnabled(FALSE);
            s->put_AreDefaultScriptDialogsEnabled(FALSE);   // ما بدنا رسائل alert() تعلّق الصفحة
        }
        p->wv->AddScriptToExecuteOnDocumentCreated(DEFAULT_CSS_SCRIPT, nullptr);

        std::weak_ptr<Page> wp = p;
        EventRegistrationToken tok{};
        // النوافذ المنبثقة: ممنوعة (ما في حدا يشوفها)
        p->wv->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT
            {
                a->put_Handled(TRUE);
                return S_OK;
            }).Get(), &tok);
        p->wv->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [wp](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT
            {
                auto pp = wp.lock();
                if (!pp || pp->closed)
                    return S_OK;
                BOOL ok = FALSE;
                a->get_IsSuccess(&ok);
                COREWEBVIEW2_WEB_ERROR_STATUS st = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                a->get_WebErrorStatus(&st);
                if (ok)
                {
                    pp->sh->failed = false;
                    pp->retryAt = 0;
                }
                else if (st != COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED)
                {
                    // ما منعرض صفحة الخطأ بالفيديو: الطبقة بتبيّن "تعذّر فتح الصفحة" ومنعيد المحاولة
                    pp->sh->failed = true;
                    pp->retryAt = GetTickCount64() + RETRY_MS;
                    std::lock_guard<std::mutex> lock(pp->sh->mutex);
                    pp->sh->frame.reset();
                }
                return S_OK;
            }).Get(), &tok);
        // الصفحة انهارت: منعيد تحميلها بعد ثانيتين
        p->wv->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
            [wp](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT
            {
                if (auto pp = wp.lock(); pp && !pp->closed)
                    pp->retryAt = GetTickCount64() + 2000;
                return S_OK;
            }).Get(), &tok);

        try
        {
            StartCapture(p);
        }
        catch (...)
        {
            Fail(*p);
            return;
        }
        p->wv->Navigate(p->url.c_str());
    }

    void OpenPage(const std::wstring& key, const std::wstring& url, int w, int h, const std::shared_ptr<Shared>& sh)
    {
        if (auto old = t_pages.find(key); old != t_pages.end())
        {
            ClosePage(*old->second);
            t_pages.erase(old);
        }
        auto p = std::make_shared<Page>();
        p->key = key; p->url = url; p->w = w; p->h = h; p->sh = sh;
        t_pages[key] = p;
        try
        {
            p->root = t_comp.CreateContainerVisual();
            p->root.Size({ (float)w, (float)h });
            p->root.IsVisible(true);
            p->holder = t_comp.CreateContainerVisual();
            p->holder.RelativeSizeAdjustment({ 1.0f, 1.0f });
            p->root.Children().InsertAtTop(p->holder);
        }
        catch (...)
        {
            Fail(*p);
            return;
        }

        std::weak_ptr<Page> wp = p;
        WithEnv([wp]
        {
            auto p = wp.lock();
            if (!p || p->closed)
                return;
            ComPtr<ICoreWebView2Environment3> env3;
            if (!t_env || FAILED(t_env.As(&env3)))
            {
                Fail(*p);
                return;
            }
            const HRESULT hr = env3->CreateCoreWebView2CompositionController(t_msgWnd,
                Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                    [wp](HRESULT r, ICoreWebView2CompositionController* cc) -> HRESULT
                    {
                        auto p = wp.lock();
                        if (!p || p->closed)
                        {
                            ComPtr<ICoreWebView2Controller> c;
                            if (cc && SUCCEEDED(cc->QueryInterface(IID_PPV_ARGS(&c))))
                                c->Close();
                            return S_OK;
                        }
                        if (FAILED(r) || !cc)
                        {
                            Fail(*p);
                            return S_OK;
                        }
                        SetupPage(p, cc);
                        return S_OK;
                    }).Get());
            if (FAILED(hr))
                Fail(*p);
        });
    }

    void ReloadAll()
    {
        for (auto& [k, p] : t_pages)
            if (p->wv && !p->closed)
                p->wv->Reload();
    }

    // كل ثانيتين: منسكّر الصفحات المتروكة ومنعيد اللي فشلت
    void OnTimer()
    {
        const ULONGLONG now = GetTickCount64();
        std::vector<std::shared_ptr<Page>> idle;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto& [k, p] : t_pages)
                if (now - p->sh->lastUsed > IDLE_CLOSE_MS)
                {
                    idle.push_back(p);
                    auto it = g_shared.find(k);
                    if (it != g_shared.end() && it->second == p->sh)
                        g_shared.erase(it);
                }
        }
        for (auto& p : idle)
        {
            ClosePage(*p);
            t_pages.erase(p->key);
        }
        for (auto& [k, p] : t_pages)
            if (p->retryAt && now >= p->retryAt && p->wv)
            {
                p->retryAt = 0;
                p->wv->Reload();
            }
    }

    // ---------------------------------------------------------
    //  نافذة التفاعل (تسجيل الدخول...)
    // ---------------------------------------------------------
    LRESULT CALLBACK InteractiveProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_SIZE:
            if (auto it = t_windows.find(hwnd); it != t_windows.end() && it->second)
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                it->second->put_Bounds(rc);
            }
            return 0;
        case WM_CLOSE:
            if (auto it = t_windows.find(hwnd); it != t_windows.end())
            {
                if (it->second) it->second->Close();
                t_windows.erase(it);
            }
            DestroyWindow(hwnd);
            ReloadAll();            // الكوكيز الجديدة (تسجيل الدخول) توصل للمصادر
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void OpenWindow(const std::wstring& url)
    {
        if (!t_env)
            return;
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSEXW wc{ sizeof(wc) };
            wc.lpfnWndProc = InteractiveProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
            wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
            if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
            wc.lpszClassName = L"SRBrowserWindow";
            RegisterClassExW(&wc);
            registered = true;
        }
        const int sw = GetSystemMetrics(SM_CXSCREEN), shh = GetSystemMetrics(SM_CYSCREEN);
        const int w = std::min(1200, sw - 80), h = std::min(850, shh - 80);
        HWND hwnd = CreateWindowExW(0, L"SRBrowserWindow", L"صفحة الويب — سجّل الدخول أو عدّل ما تريد، ثم أغلق النافذة",
                                    WS_OVERLAPPEDWINDOW, (sw - w) / 2, (shh - h) / 2, w, h,
                                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd)
            return;
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        t_windows[hwnd] = nullptr;
        t_env->CreateCoreWebView2Controller(hwnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [hwnd, url](HRESULT r, ICoreWebView2Controller* c) -> HRESULT
            {
                auto it = t_windows.find(hwnd);
                if (it == t_windows.end() || !IsWindow(hwnd))
                {
                    if (c) c->Close();
                    return S_OK;
                }
                if (FAILED(r) || !c)
                    return S_OK;
                it->second = c;
                RECT rc;
                GetClientRect(hwnd, &rc);
                c->put_Bounds(rc);
                ComPtr<ICoreWebView2> wv;
                if (SUCCEEDED(c->get_CoreWebView2(&wv)) && wv)
                    wv->Navigate(url.c_str());
                return S_OK;
            }).Get());
    }

    // ---------------------------------------------------------
    //  الخيط
    // ---------------------------------------------------------
    void ThreadMain()
    {
        auto fail = []
        {
            g_threadFailed = true;
            g_cv.notify_all();
        };
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
            return fail();

        DispatcherQueueOptions o{ sizeof(o), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
        wsy::DispatcherQueueController qc{ nullptr };
        if (FAILED(CreateDispatcherQueueController(o,
                reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(qc)))))
            return fail();

        try
        {
            t_comp = wuc::Compositor();
        }
        catch (...)
        {
            return fail();
        }

        // جهاز Direct3D خاص بالمتصفح (لنسخ الصور للذاكرة)
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                     nullptr, 0, D3D11_SDK_VERSION, &t_dev, nullptr, &t_ctx)))
            return fail();
        ComPtr<IDXGIDevice> dxgi;
        winrt::com_ptr<::IInspectable> insp;
        if (FAILED(t_dev.As(&dxgi)) || FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), insp.put())))
            return fail();
        t_wdev = insp.as<wd3::IDirect3DDevice>();

        // نافذة "رسائل" بس (WebView2 بدّو نافذة أب حتى لو ما بتنعرض)
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"SRBrowserHost";
        RegisterClassExW(&wc);
        t_msgWnd = CreateWindowExW(0, L"SRBrowserHost", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (!t_msgWnd)
            return fail();

        auto queue = qc.DispatcherQueue();
        t_timer = queue.CreateTimer();
        t_timer.Interval(std::chrono::milliseconds(2000));
        t_timer.IsRepeating(true);
        t_timer.Tick([](auto&&, auto&&) { OnTimer(); });
        t_timer.Start();

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_queue = queue;
        }
        g_cv.notify_all();

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // شغلة على خيط المتصفح
    bool Post(std::function<void()> fn)
    {
        if (!g_threadStarted.exchange(true))
            std::thread(ThreadMain).detach();
        wsy::DispatcherQueue q{ nullptr };
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait_for(lock, std::chrono::seconds(5), [] { return g_queue != nullptr || g_threadFailed; });
            q = g_queue;
        }
        if (!q)
            return false;
        try
        {
            return q.TryEnqueue([fn] { fn(); });
        }
        catch (...)
        {
            return false;
        }
    }
}

// =============================================================
bool BrowserRuntimeAvailable()
{
    static std::atomic<int> cached{ -1 };
    static std::atomic<ULONGLONG> checkedAt{ 0 };
    const ULONGLONG now = GetTickCount64();
    if (cached == 1 || (cached == 0 && now - checkedAt < 10'000))
        return cached == 1;
    LPWSTR v = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &v);
    const bool ok = SUCCEEDED(hr) && v && *v;
    if (v) CoTaskMemFree(v);
    cached = ok ? 1 : 0;
    checkedAt = now;
    return ok;
}

std::shared_ptr<const MediaFrame> BrowserFrame(const std::wstring& url, int width, int height, bool* missing)
{
    if (missing)
        *missing = false;
    width  = std::clamp(width, 16, 3840);
    height = std::clamp(height, 16, 3840);
    if (url.empty() || g_threadFailed || !BrowserRuntimeAvailable())
    {
        if (missing) *missing = true;
        return nullptr;
    }

    const std::wstring key = Key(url, width, height);
    std::shared_ptr<Shared> sh;
    bool create = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto& slot = g_shared[key];
        if (!slot)
        {
            slot = std::make_shared<Shared>();
            create = true;
        }
        sh = slot;
        sh->lastUsed = GetTickCount64();
    }
    if (create && !Post([key, url, width, height, sh] { OpenPage(key, url, width, height, sh); }))
        sh->failed = true;

    if (missing)
        *missing = sh->failed;
    if (sh->failed)
        return nullptr;
    std::lock_guard<std::mutex> lock(sh->mutex);
    return sh->frame;
}

void BrowserReload(const std::wstring& url, int width, int height)
{
    const std::wstring key = Key(url, std::clamp(width, 16, 3840), std::clamp(height, 16, 3840));
    if (!g_threadStarted)
        return;
    Post([key]
    {
        auto it = t_pages.find(key);
        if (it != t_pages.end() && it->second->wv)
        {
            it->second->sh->failed = false;
            it->second->wv->Reload();
        }
    });
}

void BrowserOpenInteractive(const std::wstring& url)
{
    if (url.empty() || !BrowserRuntimeAvailable())
        return;
    std::thread([url] { Post([url] { WithEnv([url] { OpenWindow(url); }); }); }).detach();   // ما منعلّق الواجهة
}

std::wstring NormalizeUrl(const std::wstring& typed)
{
    size_t a = typed.find_first_not_of(L" \t\r\n\"");
    size_t b = typed.find_last_not_of(L" \t\r\n\"");
    if (a == std::wstring::npos)
        return L"";
    std::wstring s = typed.substr(a, b - a + 1);
    if (s.find(L"://") != std::wstring::npos || s.rfind(L"about:", 0) == 0 || s.rfind(L"data:", 0) == 0)
        return s;
    const DWORD attr = GetFileAttributesW(s.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))     // ملف HTML على الجهاز
    {
        std::wstring u = L"file:///" + s;
        std::replace(u.begin() + 8, u.end(), L'\\', L'/');
        return u;
    }
    return L"https://" + s;
}

std::wstring UrlDisplayName(const std::wstring& url)
{
    if (url.rfind(L"file:///", 0) == 0)
    {
        const size_t slash = url.find_last_of(L'/');
        return url.substr(slash + 1);
    }
    size_t start = url.find(L"://");
    start = start == std::wstring::npos ? 0 : start + 3;
    size_t end = url.find_first_of(L"/?#", start);
    std::wstring host = url.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    if (host.rfind(L"www.", 0) == 0)
        host = host.substr(4);
    return host.empty() ? std::wstring(L"صفحة ويب") : host;
}
