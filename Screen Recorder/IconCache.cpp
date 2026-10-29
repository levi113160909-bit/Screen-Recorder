// =============================================================
//  IconCache.cpp
// =============================================================
#include "IconCache.h"
#include <shellapi.h>
#include <shlobj.h>
#include <cwctype>

#pragma comment(lib, "windowscodecs.lib")

namespace
{
    std::wstring Lower(std::wstring s)
    {
        for (wchar_t& c : s) c = (wchar_t)std::towlower(c);
        return s;
    }
}

IconCache::IconCache()
{
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wic));
}

bool IconCache::CheckTarget()
{
    if (m_rt.Get() != (ID2D1RenderTarget*)g_rt.Get())
    {
        m_rt = g_rt;
        m_cache.clear();
        m_tried.clear();
    }
    return m_rt && m_wic;
}

ID2D1Bitmap* IconCache::FromIcon(HICON icon)
{
    ComPtr<IWICBitmap>          wicBmp;
    ComPtr<IWICFormatConverter> conv;
    ComPtr<ID2D1Bitmap>         bmp;
    if (SUCCEEDED(m_wic->CreateBitmapFromHICON(icon, &wicBmp)) &&
        SUCCEEDED(m_wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(wicBmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                   nullptr, 0, WICBitmapPaletteTypeMedianCut)))
        g_rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &bmp);
    return bmp.Detach();       // اللي بيستدعي بيحطها بالـ cache
}

ID2D1Bitmap* IconCache::FromExe(const std::wstring& exePath)
{
    if (!CheckTarget() || exePath.empty())
        return nullptr;
    const std::wstring key = L"exe:" + Lower(exePath);
    if (m_tried[key])
        return m_cache[key].Get();
    m_tried[key] = true;

    // أيقونة كبيرة (48) لحتى تبيّن حلوة على الشاشات العالية الدقة
    HICON icon = nullptr;
    if (FAILED(SHDefExtractIconW(exePath.c_str(), 0, 0, &icon, nullptr, MAKELONG(48, 16))) || !icon)
    {
        SHFILEINFOW info{};
        if (SHGetFileInfoW(exePath.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON))
            icon = info.hIcon;
    }
    if (!icon)
        return nullptr;
    ComPtr<ID2D1Bitmap> bmp;
    bmp.Attach(FromIcon(icon));
    DestroyIcon(icon);
    m_cache[key] = bmp;
    return bmp.Get();
}

ID2D1Bitmap* IconCache::FromWindow(HWND hwnd, const std::wstring& exePath)
{
    // أيقونة البرنامج أوضح وأكبر من أيقونة النافذة الصغيرة، فمنفضّلها
    if (ID2D1Bitmap* b = FromExe(exePath))
        return b;
    if (!CheckTarget() || !hwnd)
        return nullptr;
    const std::wstring key = L"wnd:" + std::to_wstring((UINT_PTR)hwnd);
    if (m_tried[key])
        return m_cache[key].Get();
    m_tried[key] = true;
    DWORD_PTR result = 0;
    HICON icon = nullptr;
    if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, &result) && result)
        icon = (HICON)result;
    if (!icon)
        icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (!icon)
        return nullptr;
    ComPtr<ID2D1Bitmap> bmp;
    bmp.Attach(FromIcon(icon));     // أيقونة النافذة مش إلنا: ما منحذفها
    m_cache[key] = bmp;
    return bmp.Get();
}

ID2D1Bitmap* IconCache::FromPixels(const std::wstring& key, int w, int h, const std::vector<BYTE>& bgra)
{
    if (!CheckTarget() || w <= 0 || h <= 0 || bgra.size() < (size_t)w * h * 4)
        return nullptr;
    const std::wstring k = L"px:" + key;
    if (m_tried[k])
        return m_cache[k].Get();
    m_tried[k] = true;
    ComPtr<ID2D1Bitmap> bmp;
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    g_rt->CreateBitmap(D2D1::SizeU(w, h), bgra.data(), w * 4, props, &bmp);
    m_cache[k] = bmp;
    return bmp.Get();
}

std::vector<BYTE> CaptureScreenThumb(const RECT& r, int w, int h)
{
    std::vector<BYTE> pixels;
    HDC screen = GetDC(nullptr);
    HDC mem    = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          // من فوق لتحت
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits)
    {
        HGDIOBJ old = SelectObject(mem, dib);
        SetStretchBltMode(mem, HALFTONE);       // تصغير ناعم
        SetBrushOrgEx(mem, 0, 0, nullptr);
        StretchBlt(mem, 0, 0, w, h, screen, r.left, r.top, r.right - r.left, r.bottom - r.top, SRCCOPY | CAPTUREBLT);
        GdiFlush();
        pixels.assign((BYTE*)bits, (BYTE*)bits + (size_t)w * h * 4);
        SelectObject(mem, old);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return pixels;
}
