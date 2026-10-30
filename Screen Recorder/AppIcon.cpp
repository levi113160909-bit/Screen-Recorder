// =============================================================
//  AppIcon.cpp - أيقونة البرنامج بشريط المهام (منرسمها بالكود، وبتتغيّر وقت التسجيل)
// =============================================================
#include "App.h"

HICON MakeAppIcon(int size, int state)
{
    const UINT32 bgColor[] = { 0x16191F, 0xE5484D, 0xF5A524 };
    std::vector<UINT32> px((size_t)size * size);
    const int SS = 4;   // 4×4 عينات لكل بكسل = حواف ناعمة

    for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
    {
        double r = 0, g = 0, b = 0;
        int inside = 0;
        for (int sy = 0; sy < SS; ++sy)
        for (int sx = 0; sx < SS; ++sx)
        {
            const double fx = (x + (sx + 0.5) / SS) / size - 0.5;   // من -0.5 لـ 0.5
            const double fy = (y + (sy + 0.5) / SS) / size - 0.5;

            // مربع بزوايا مدوّرة
            const double rad = 0.22, half = 0.5 - 0.02;
            const double qx = std::fabs(fx) - (half - rad), qy = std::fabs(fy) - (half - rad);
            const double dist = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) - rad;
            if (dist > 0)
                continue;

            UINT32 c = bgColor[state];
            const double d = std::hypot(fx, fy);
            if (state == 0)                       // جاهز: دائرة حمرا ونقطة بيضا
            {
                if (d < 0.30) c = 0xE5484D;
                if (d < 0.11) c = 0xFFFFFF;
            }
            else if (state == 1)                  // عم يسجّل: دائرة بيضا كبيرة
            {
                if (d < 0.27) c = 0xFFFFFF;
            }
            else                                  // متوقف مؤقتاً: خطّين أبيض
            {
                if (std::fabs(fy) < 0.22 && (std::fabs(fx - 0.11) < 0.06 || std::fabs(fx + 0.11) < 0.06))
                    c = 0xFFFFFF;
            }
            r += (c >> 16) & 0xFF;
            g += (c >> 8) & 0xFF;
            b += c & 0xFF;
            ++inside;
        }
        if (inside == 0)
            continue;
        const UINT32 a = inside * 255 / (SS * SS);
        px[(size_t)y * size + x] = (a << 24) | ((UINT32)(r / inside) << 16) | ((UINT32)(g / inside) << 8) | (UINT32)(b / inside);
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = size;
    bmi.bmiHeader.biHeight      = -size;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color)
        return nullptr;
    memcpy(bits, px.data(), px.size() * 4);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);

    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

void CreateAppIcons()
{
    const int big   = GetSystemMetricsForDpi(SM_CXICON, g_dpi);
    const int smallSize = GetSystemMetricsForDpi(SM_CXSMICON, g_dpi);   // (small كلمة محجوزة بويندوز)
    for (int s = 0; s < 3; ++s)
    {
        g_icons[s][0] = MakeAppIcon(big, s);
        g_icons[s][1] = MakeAppIcon(smallSize, s);
    }
}

void UpdateAppIcon()
{
    // 0 جاهز ، 1 عم يسجّل ، 2 متوقف مؤقتاً ، 3 بث مباشر بس (بدون تسجيل)
    int state = !g_recorder.IsRecording() ? 0 : (g_recorder.IsPaused() ? 2 : 1);
    if (state == 0 && g_streamer.IsActive())
        state = 3;
    if (state == g_iconState)
        return;
    g_iconState = state;
    const int icon = state == 3 ? 1 : state;
    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG,   (LPARAM)g_icons[icon][0]);
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icons[icon][1]);
    const wchar_t* titles[] = { L"مسجّل الشاشة", L"● جارٍ التسجيل - مسجّل الشاشة",
                                L"❚❚ متوقف مؤقتاً - مسجّل الشاشة", L"● بث مباشر - مسجّل الشاشة" };
    SetWindowTextW(g_hwnd, titles[state]);
    g_tray.SetIcon(g_icons[icon][1]);      // أيقونة جنب الساعة كمان
    g_tray.SetTip(titles[state]);
}
