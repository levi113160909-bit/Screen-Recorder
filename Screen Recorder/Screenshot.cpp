// =============================================================
//  Screenshot.cpp - لقطة شاشة (شوف Screenshot.h)
// =============================================================
#include "Screenshot.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    std::atomic<bool> g_busy{ false };

    bool SavePng(const std::wstring& path, const BYTE* bgra, int w, int h)
    {
        ComPtr<IWICImagingFactory> f;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))))
            return false;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> enc;
        ComPtr<IWICBitmapFrameEncode> frame;
        HRESULT hr = f->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        if (SUCCEEDED(hr)) hr = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGR;      // بلا شفافية (الصورة كاملة)
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
        if (SUCCEEDED(hr)) hr = frame->WritePixels(h, w * 4, w * h * 4, const_cast<BYTE*>(bgra));
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = enc->Commit();
        return SUCCEEDED(hr);
    }

    // صورة للحافظة (DIB من تحت لفوق)
    HGLOBAL MakeDib(const BYTE* bgra, int w, int h)
    {
        const size_t bytes = sizeof(BITMAPINFOHEADER) + (size_t)w * h * 4;
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!g)
            return nullptr;
        auto* p = static_cast<BYTE*>(GlobalLock(g));
        BITMAPINFOHEADER bi{};
        bi.biSize = sizeof(bi);
        bi.biWidth = w;
        bi.biHeight = h;                // موجب = من تحت لفوق
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        memcpy(p, &bi, sizeof(bi));
        BYTE* dst = p + sizeof(bi);
        for (int y = 0; y < h; ++y)
        {
            BYTE* row = dst + (size_t)(h - 1 - y) * w * 4;
            memcpy(row, bgra + (size_t)y * w * 4, (size_t)w * 4);
            for (int x = 0; x < w; ++x) row[x * 4 + 3] = 255;
        }
        GlobalUnlock(g);
        return g;
    }
}

void TakeScreenshot(Scene& scene, const std::wstring& folder, HWND notify)
{
    if (g_busy.exchange(true))
        return;                         // لقطة لسا عم تنحفظ
    int cw = 0, ch = 0;
    std::vector<SceneLayer> layers;
    scene.Snapshot(cw, ch, layers);
    std::thread([layers, cw, ch, folder, notify]
    {
        const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::wstring* path = nullptr;
        HGLOBAL dib = nullptr;
        if (cw >= 16 && ch >= 16)
        {
            std::vector<BYTE> px((size_t)cw * ch * 4);
            {
                Compositor comp(false);                     // بلا مؤشر الماوس
                comp.Compose(layers, cw, ch, px.data(), cw, ch, cw * 4, false);
                Sleep(60);                                  // أول صورة من كل مصدر بتاخد شوي
                comp.Compose(layers, cw, ch, px.data(), cw, ch, cw * 4, false);
            }
            const std::wstring dir = folder + L"\\Screenshots";
            CreateDirectoryW(folder.c_str(), nullptr);
            CreateDirectoryW(dir.c_str(), nullptr);
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t name[96];
            swprintf_s(name, L"\\Screenshot_%04d-%02d-%02d_%02d-%02d-%02d.png",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            const std::wstring file = dir + name;
            if (SavePng(file, px.data(), cw, ch))
            {
                path = new std::wstring(file);
                dib = MakeDib(px.data(), cw, ch);
            }
        }
        if (SUCCEEDED(hrCom))
            CoUninitialize();
        g_busy = false;
        if (!PostMessageW(notify, WM_SCREENSHOT_DONE, (WPARAM)dib, (LPARAM)path))
        {
            delete path;
            if (dib) GlobalFree(dib);
        }
    }).detach();
}

std::wstring FinishScreenshot(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
    HGLOBAL dib = reinterpret_cast<HGLOBAL>(wParam);
    if (dib)
    {
        if (OpenClipboard(hwnd))
        {
            EmptyClipboard();
            if (SetClipboardData(CF_DIB, dib))
                dib = nullptr;          // صارت ملك الحافظة
            CloseClipboard();
        }
        if (dib) GlobalFree(dib);
    }
    return path ? *path : std::wstring();
}
