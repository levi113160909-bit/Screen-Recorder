// =============================================================
//  IconCache.h - صور صغيرة للواجهة (أيقونات البرامج، صور الشاشات)
//
//  الصورة بتنعمل مرة وحدة وبتنحفظ. وإذا الرسّام تغيّر
//  (مثلاً بعد تغيير الدقة) منعيد نعملها لحالها.
// =============================================================
#pragma once
#include "Ui.h"
#include <map>
#include <wincodec.h>

class IconCache
{
public:
    IconCache();

    // أيقونة برنامج من ملف الـ exe
    ID2D1Bitmap* FromExe(const std::wstring& exePath);
    // أيقونة نافذة (أيقونتها الخاصة، وإلا أيقونة برنامجها)
    ID2D1Bitmap* FromWindow(HWND hwnd, const std::wstring& exePath);
    // صورة جاهزة (BGRA): مثلاً لقطة مصغّرة للشاشة
    ID2D1Bitmap* FromPixels(const std::wstring& key, int w, int h, const std::vector<BYTE>& bgra);

private:
    bool         CheckTarget();                  // الرسّام تغيّر؟ منمسح الصور
    ID2D1Bitmap* FromIcon(HICON icon);

    ComPtr<IWICImagingFactory>                  m_wic;
    ComPtr<ID2D1RenderTarget>                   m_rt;
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> m_cache;
    std::map<std::wstring, bool>                m_tried;
};

// لقطة مصغّرة لجزء من الشاشة (بالبكسل الحقيقي). بترجّع BGRA
std::vector<BYTE> CaptureScreenThumb(const RECT& r, int w, int h);
