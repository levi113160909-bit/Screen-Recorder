// =============================================================
//  ScreenTranslate.cpp - ترجمة الشاشة كلها بكبسة زر (شوف ScreenTranslate.h)
// =============================================================
#include "ScreenTranslate.h"
#include "GameOcr.h"
#include "Media.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <thread>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr int       THUMB_W      = 480;     // صورة صغيرة لنعرف إذا الشاشة تغيّرت
    constexpr ULONGLONG CHECK_MS     = 400;     // كل قديش منشيك
    constexpr int       CHANGE_LUM   = 22;      // فرق السطوع اللي بيعني "تغيّر"
    constexpr int       MAX_ITEMS    = 80;      // أكتر من هيك نصوص بشاشة وحدة: منكتفي

    // فقرة نص بمكانها (بكسل صورة المصدر)
    struct Block
    {
        float        x = 0, y = 0, w = 0, h = 0;
        float        lineH = 0;          // ارتفاع السطر (لحجم الخط)
        int          lines = 1;
        float        lastX = 0, lastY = 0, lastW = 0, lastH = 0;
        size_t       lastLen = 0;
        std::wstring en, ar;
    };

    struct Thumb
    {
        int w = 0, h = 0;
        std::vector<uint8_t> lum;
    };

    struct State
    {
        std::mutex   mx;
        uint64_t     job = 0;
        bool         working = false, showing = false, finished = false;
        bool         fromScreen = true;
        RECT         monitor{}, playerMonitor{};
        std::wstring deviceLink, profileId;
        int          srcW = 0, srcH = 0;
        std::shared_ptr<const MediaFrame> streamFrame, playerFrame;
        ULONGLONG    captureTick = 0, hideTick = 0;
        int          delayMs = 0;
        int          count = 0;
        std::wstring error;
    };
    State             g;
    std::atomic<bool> g_shutdown{ false };
    std::atomic<uint64_t> g_version{ 0x7800000000000000ull };

    // نافذتك فوق الشاشة (الخيط الرئيسي بس)
    HWND      g_wnd = nullptr;
    bool      g_wndShown = false;
    bool      g_wndBlocked = false;
    uint64_t  g_wndVersion = 0;
    RECT      g_wndRect{};
    ULONGLONG g_wndTop = 0;
    HWND      g_lastFg = nullptr;

    ULONGLONG Now() { return GetTickCount64(); }

    std::shared_ptr<const MediaFrame> Transparent()
    {
        static std::shared_ptr<const MediaFrame> f = []
        {
            auto t = std::make_shared<MediaFrame>();
            t->width = t->height = 2;
            t->pixels.assign(16, 0);
            t->version = 0x77FFFFFFFFFFFFFFull;
            return t;
        }();
        return f;
    }

    int AsciiLetters(const std::wstring& s)
    {
        int n = 0;
        for (wchar_t c : s)
            if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) ++n;
        return n;
    }

    bool HasArabic(const std::wstring& s)
    {
        for (wchar_t c : s)
            if (c >= 0x0600 && c <= 0x06FF) return true;
        return false;
    }

    // ---------- صورة صغيرة للمقارنة ----------
    Thumb MakeThumb(const uint8_t* bgra, int w, int h, int stride, int tw)
    {
        Thumb t;
        if (!bgra || w < 4 || h < 4)
            return t;
        const int step = std::max(1, w / tw);
        t.w = w / step;
        t.h = h / step;
        t.lum.resize((size_t)t.w * t.h);
        for (int y = 0; y < t.h; ++y)
            for (int x = 0; x < t.w; ++x)
            {
                const uint8_t* p = bgra + (size_t)y * step * stride + (size_t)x * step * 4;
                t.lum[(size_t)y * t.w + x] = (uint8_t)((p[2] * 2 + p[1] * 5 + p[0]) / 8);
            }
        return t;
    }

    // صورة المصدر الحالية مصغّرة (الشاشة بتنصوّر مصغّرة من الأساس)
    Thumb CurrentThumb(bool fromScreen, const RECT& monitor, const std::wstring& link)
    {
        if (fromScreen)
        {
            std::vector<uint8_t> px;
            int w = 0, h = 0;
            if (!GrabScreenArea(monitor, px, w, h, THUMB_W))
                return {};
            return MakeThumb(px.data(), w, h, w * 4, w);
        }
        std::shared_ptr<const MediaFrame> f = PeekDeviceFrame(link);
        if (!f || f->width < 16)
            return {};
        return MakeThumb(f->pixels.data(), f->width, f->height, f->width * 4, THUMB_W);
    }

    // كم نص تغيّر مكانه؟ (نسبة 0..1)
    double ChangedShare(const Thumb& a, const Thumb& b, const std::vector<Block>& blocks, int srcW, int srcH)
    {
        if (a.w != b.w || a.h != b.h || a.w < 4 || blocks.empty() || srcW < 1 || srcH < 1)
            return 1.0;
        const double sx = (double)a.w / srcW, sy = (double)a.h / srcH;
        int changed = 0;
        for (const Block& bl : blocks)
        {
            const int x0 = std::clamp((int)(bl.x * sx), 0, a.w - 1), x1 = std::clamp((int)std::ceil((bl.x + bl.w) * sx), x0 + 1, a.w);
            const int y0 = std::clamp((int)(bl.y * sy), 0, a.h - 1), y1 = std::clamp((int)std::ceil((bl.y + bl.h) * sy), y0 + 1, a.h);
            long long sum = 0, n = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                {
                    sum += std::abs((int)a.lum[(size_t)y * a.w + x] - (int)b.lum[(size_t)y * b.w + x]);
                    ++n;
                }
            if (n && sum / n > CHANGE_LUM)
                ++changed;
        }
        return (double)changed / blocks.size();
    }

    // ---------- الأسطر -> فقرات ----------
    // سطر طويل وتحته سطر بنفس الحجم وقريب منه = نفس الفقرة (نص ملفوف).
    // الأسطر القصيرة (أزرار وقوائم) كل واحد لحاله
    std::vector<Block> GroupLines(std::vector<OcrLineBox> lines)
    {
        std::sort(lines.begin(), lines.end(), [](const OcrLineBox& a, const OcrLineBox& b) { return a.y < b.y; });
        std::vector<Block> blocks;
        for (const OcrLineBox& l : lines)
        {
            if (AsciiLetters(l.text) < 2 || HasArabic(l.text))
                continue;
            Block* into = nullptr;
            for (auto it = blocks.rbegin(); it != blocks.rend() && !into; ++it)
            {
                Block& b = *it;
                const float hmax = std::max(b.lastH, l.h);
                const float gap = l.y - (b.lastY + b.lastH);
                const bool sameSize = std::fabs(b.lastH - l.h) <= hmax * 0.35f;
                const bool close    = gap > -hmax * 0.3f && gap < hmax * 0.9f;
                const bool overlap  = l.x < b.lastX + b.lastW && l.x + l.w > b.lastX;
                const bool wrapped  = b.lastLen >= 28;           // السطر اللي فوق طويل: نص ملفوف
                if (sameSize && close && overlap && wrapped)
                    into = &b;
            }
            if (into)
            {
                const float x1 = std::max(into->x + into->w, l.x + l.w), y1 = std::max(into->y + into->h, l.y + l.h);
                into->x = std::min(into->x, l.x);
                into->y = std::min(into->y, l.y);
                into->w = x1 - into->x;
                into->h = y1 - into->y;
                into->lineH = (into->lineH * into->lines + l.h) / (into->lines + 1);
                ++into->lines;
                into->en += L" " + l.text;
                into->lastX = l.x; into->lastY = l.y; into->lastW = l.w; into->lastH = l.h;
                into->lastLen = l.text.size();
            }
            else
            {
                Block b;
                b.x = l.x; b.y = l.y; b.w = l.w; b.h = l.h;
                b.lineH = l.h;
                b.en = l.text;
                b.lastX = l.x; b.lastY = l.y; b.lastW = l.w; b.lastH = l.h;
                b.lastLen = l.text.size();
                blocks.push_back(b);
            }
        }
        if ((int)blocks.size() > MAX_ITEMS)
            blocks.resize(MAX_ITEMS);
        return blocks;
    }

    // ---------- الرسم: كل المربعات بصورة وحدة بحجم W×H ----------
    std::shared_ptr<const MediaFrame> Render(const std::vector<Block>& blocks, int srcW, int srcH, int W, int H,
                                             const TranslateProfile& p)
    {
        static ComPtr<ID2D1Factory>   d2d;
        static ComPtr<IDWriteFactory> dw;
        static std::mutex             factoryMx;
        {
            std::lock_guard<std::mutex> lock(factoryMx);
            if (!d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, d2d.GetAddressOf())))
                return nullptr;
            if (!dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                  reinterpret_cast<IUnknown**>(dw.GetAddressOf()))))
                return nullptr;
        }
        if (W < 16 || H < 16 || srcW < 1 || srcH < 1)
            return nullptr;
        ComPtr<IWICImagingFactory> wic;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
            return nullptr;
        ComPtr<IWICBitmap> bitmap;
        if (FAILED(wic->CreateBitmap((UINT)W, (UINT)H, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)))
            return nullptr;
        ComPtr<ID2D1RenderTarget> rt;
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt)))
            return nullptr;
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        const unsigned c = TransColor(p.colorIndex);
        ComPtr<ID2D1SolidColorBrush> text, box;
        rt->CreateSolidColorBrush(D2D1::ColorF(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f), &text);
        // لازم يغطّي الإنكليزي منيح (القوائم خلفيتها مزدحمة)
        rt->CreateSolidColorBrush(D2D1::ColorF(0x0B0D12, std::max(0.9f, p.boxOpacity)), &box);
        if (!text || !box)
            return nullptr;

        const float sx = (float)W / srcW, sy = (float)H / srcH;
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        for (const Block& b : blocks)
        {
            if (b.ar.empty())
                continue;
            const float lineH = std::max(6.0f, b.lineH * sy);
            const float pad = std::max(2.0f, lineH * 0.18f);
            D2D1_RECT_F r = D2D1::RectF(b.x * sx - pad, b.y * sy - pad, (b.x + b.w) * sx + pad, (b.y + b.h) * sy + pad);
            const float maxW = std::max(10.0f, r.right - r.left - pad);

            // الخط: بحجم الإنكليزي تقريباً ، ومنصغّره لحد ما يوسع
            float size = lineH * 0.85f * p.fontScale;
            ComPtr<IDWriteTextLayout> layout;
            DWRITE_TEXT_METRICS m{};
            for (int i = 0; i < 16; ++i)
            {
                ComPtr<IDWriteTextFormat> format;
                if (FAILED(dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, std::max(8.0f, size), L"ar", &format)))
                    break;
                format->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
                format->SetTextAlignment(b.lines > 1 ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_CENTER);
                format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                layout.Reset();
                if (FAILED(dw->CreateTextLayout(b.ar.c_str(), (UINT32)b.ar.size(), format.Get(), maxW, 10000, &layout)))
                    break;
                layout->GetMetrics(&m);
                const float boxH = r.bottom - r.top - pad;
                if ((m.height <= boxH * 1.1f && m.widthIncludingTrailingWhitespace <= maxW + 1) || size <= 8)
                    break;
                size *= 0.9f;
            }
            if (!layout)
                continue;
            // ما وسع حتى بأصغر خط: منطوّل المربع لتحت
            const float need = m.height + pad;
            if (r.bottom - r.top < need)
                r.bottom = std::min((float)H, r.top + need);
            layout->SetMaxHeight(r.bottom - r.top);
            const float rad = std::min(6.0f, lineH * 0.25f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), box.Get());
            rt->DrawTextLayout(D2D1::Point2F(r.left + pad / 2, r.top), layout.Get(), text.Get());
        }
        if (FAILED(rt->EndDraw()))
            return nullptr;

        auto frame = std::make_shared<MediaFrame>();
        frame->width  = W;
        frame->height = H;
        frame->pixels.resize((size_t)W * H * 4);
        if (FAILED(bitmap->CopyPixels(nullptr, (UINT)W * 4, (UINT)frame->pixels.size(), frame->pixels.data())))
            return nullptr;
        frame->version = ++g_version;
        frame->tick    = Now();
        return frame;
    }

    void Finish(uint64_t job, const std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(g.mx);
        if (g.job != job)
            return;
        g.working  = false;
        g.finished = true;
        g.error    = error;
    }

    // ---------- الشغل كله (بخيط بالخلفية) ----------
    void Worker(uint64_t job, TranslateProfile p, bool fromScreen, RECT monitor, RECT playerMonitor)
    {
        const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        [&]
        {
            // 1) الصورة
            std::vector<uint8_t> px;
            int w = 0, h = 0;
            ULONGLONG tick = Now();
            Thumb snap;
            if (fromScreen)
            {
                snap = CurrentThumb(true, monitor, L"");
                if (!GrabScreenArea(monitor, px, w, h, 0))
                    return Finish(job, L"تعذّر التقاط الشاشة");
            }
            else
            {
                std::shared_ptr<const MediaFrame> f = PeekDeviceFrame(p.deviceLink);
                if (!f || f->width < 16 || f->height < 16)
                    return Finish(job, L"لا تصل صورة من جهاز الالتقاط");
                w = f->width;
                h = f->height;
                px = f->pixels;
                if (f->tick) tick = f->tick;
                snap = MakeThumb(px.data(), w, h, w * 4, THUMB_W);
            }

            // 2) القراءة مع الأماكن
            OcrReader ocr;
            if (!ocr.Ready())
                return Finish(job, L"قارئ النصوص غير متاح في ويندوز");
            std::vector<OcrLineBox> lines;
            if (!ocr.ReadLines(px.data(), w, h, w * 4, lines))
                return Finish(job, L"تعذّرت قراءة الشاشة");
            px = {};
            std::vector<Block> blocks = GroupLines(std::move(lines));
            if (blocks.empty())
                return Finish(job, L"لم يُعثر على نص إنكليزي في الشاشة");
            {
                std::lock_guard<std::mutex> lock(g.mx);
                if (g.job != job) return;
            }

            // 3) الترجمة (طلب واحد)
            std::vector<ScreenTextItem> items(blocks.size());
            for (size_t i = 0; i < blocks.size(); ++i)
                items[i].en = blocks[i].en;
            std::wstring error;
            const bool ok = TranslateScreenTexts(p, items, error);
            int count = 0;
            for (size_t i = 0; i < blocks.size(); ++i)
            {
                blocks[i].ar = items[i].ar;
                if (!blocks[i].ar.empty()) ++count;
            }
            if (!ok || count == 0)
                return Finish(job, error.empty() ? L"تعذّرت الترجمة" : error);

            // 4) الرسم: للبث بحجم المصدر ، وإلك بحجم شاشتك
            std::shared_ptr<const MediaFrame> stream = Render(blocks, w, h, w, h, p);
            const int pw = playerMonitor.right - playerMonitor.left, ph = playerMonitor.bottom - playerMonitor.top;
            std::shared_ptr<const MediaFrame> player = (pw == w && ph == h) ? stream : Render(blocks, w, h, pw, ph, p);
            if (!stream)
                return Finish(job, L"تعذّر رسم الترجمة");
            {
                std::lock_guard<std::mutex> lock(g.mx);
                if (g.job != job) return;
                g.srcW        = w;
                g.srcH        = h;
                g.streamFrame = stream;
                g.playerFrame = player;
                g.captureTick = tick;
                g.hideTick    = 0;
                g.showing     = true;
                g.working     = false;
                g.finished    = true;
                g.count       = count;
                g.error       = count < (int)blocks.size() ? error : std::wstring();   // ترجمة ناقصة: منقلّك ليش
            }

            // 5) منراقب: الشاشة تغيّرت مكان النصوص (سكّرت القائمة)؟ منخفي
            int strikes = 0;
            while (!g_shutdown)
            {
                Sleep((DWORD)CHECK_MS);
                {
                    std::lock_guard<std::mutex> lock(g.mx);
                    if (g.job != job || !g.showing) return;
                }
                const Thumb now = CurrentThumb(fromScreen, monitor, p.deviceLink);
                if (now.w == 0)
                    continue;
                strikes = ChangedShare(snap, now, blocks, w, h) >= 0.4 ? strikes + 1 : 0;
                if (strikes >= 2)
                {
                    std::lock_guard<std::mutex> lock(g.mx);
                    if (g.job == job && g.showing)
                    {
                        g.showing  = false;
                        g.hideTick = Now() - CHECK_MS;      // تغيّرت تقريباً من فحصتين
                    }
                    return;
                }
            }
        }();
        if (SUCCEEDED(hrCom))
            CoUninitialize();
    }

    // ---------- نافذتك ----------
    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCHITTEST)
            return HTTRANSPARENT;
        if (msg == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool EnsureWindow()
    {
        if (g_wnd)
            return true;
        if (g_wndBlocked)
            return false;
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc   = WndProc;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"ScreenRecorderScreenTranslate";
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassW(&wc);
            registered = true;
        }
        g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                L"ScreenRecorderScreenTranslate", L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
        if (!g_wnd)
            return false;
        // لازم تنخبّى عن التصوير: وإلا كانت رح تطلع مرتين بالبث ، وفحص "تغيّرت الشاشة" كان رح يشوفها
        if (!SetWindowDisplayAffinity(g_wnd, WDA_EXCLUDEFROMCAPTURE))
        {
            DestroyWindow(g_wnd);
            g_wnd = nullptr;
            g_wndBlocked = true;
            return false;
        }
        return true;
    }

    void HideWindow()
    {
        if (g_wnd && g_wndShown)
        {
            ShowWindow(g_wnd, SW_HIDE);
            g_wndShown = false;
        }
    }

    void Present(const MediaFrame& f, const RECT& r)
    {
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = f.width;
        bmi.bmiHeader.biHeight      = -f.height;
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib && bits)
        {
            memcpy(bits, f.pixels.data(), (size_t)f.width * f.height * 4);
            HGDIOBJ old = SelectObject(mem, dib);
            POINT src{ 0, 0 }, dst{ r.left, r.top };
            SIZE  size{ f.width, f.height };
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            UpdateLayeredWindow(g_wnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
            SelectObject(mem, old);
        }
        if (dib)
            DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }
}

void StartScreenTranslate(const TranslateProfile& p, bool fromScreen, const RECT& monitor, const RECT& playerMonitor)
{
    uint64_t job;
    {
        std::lock_guard<std::mutex> lock(g.mx);
        job = ++g.job;
        g.working       = true;
        g.showing       = false;
        g.finished      = false;
        g.fromScreen    = fromScreen;
        g.monitor       = monitor;
        g.playerMonitor = playerMonitor;
        g.deviceLink    = fromScreen ? std::wstring() : p.deviceLink;
        g.profileId     = p.id;
        g.delayMs       = fromScreen ? 0 : std::max(0, p.delayMs);   // التقاط الشاشة ما بيتأخّر
        g.streamFrame.reset();
        g.playerFrame.reset();
        g.hideTick      = 0;
        g.count         = 0;
        g.error.clear();
    }
    std::thread(Worker, job, p, fromScreen, monitor, playerMonitor).detach();
}

void StopScreenTranslate()
{
    std::lock_guard<std::mutex> lock(g.mx);
    ++g.job;                                  // المحاولة اللي شغّالة ما عاد تنشر شي
    if (g.showing)
        g.hideTick = Now();
    g.showing = false;
    g.working = false;
}

ScreenTranslateInfo GetScreenTranslateInfo()
{
    std::lock_guard<std::mutex> lock(g.mx);
    ScreenTranslateInfo i;
    i.working    = g.working;
    i.showing    = g.showing;
    i.fromScreen = g.fromScreen;
    i.monitor    = g.monitor;
    i.deviceLink = g.deviceLink;
    i.profileId  = g.profileId;
    i.job        = g.job;
    i.finished   = g.finished;
    i.count      = g.count;
    i.error      = g.error;
    const ULONGLONG seen = Now() - (ULONGLONG)g.delayMs;     // الوقت اللي عم يشوفه المشاهد
    i.streamLayer = g.streamFrame && (g.showing || (g.hideTick && seen < g.hideTick));
    return i;
}

std::shared_ptr<const MediaFrame> ScreenTranslateLayerFrame()
{
    std::lock_guard<std::mutex> lock(g.mx);
    if (!g.streamFrame)
        return Transparent();
    // المشاهد متأخّر: منبيّنها لما توصله الصورة اللي انقرت ، ومنخفيها لما توصله اللحظة اللي تغيّرت فيها
    const ULONGLONG seen = Now() - (ULONGLONG)g.delayMs;
    const bool visible = seen >= g.captureTick && (g.showing || (g.hideTick && seen < g.hideTick));
    return visible ? g.streamFrame : Transparent();
}

void UpdateScreenTranslate()
{
    std::shared_ptr<const MediaFrame> f;
    RECT r{};
    {
        std::lock_guard<std::mutex> lock(g.mx);
        if (g.showing)
        {
            f = g.playerFrame;
            r = g.playerMonitor;
        }
    }
    if (!f || f->width < 1)
    {
        HideWindow();
        return;
    }
    if (!EnsureWindow())
        return;
    if (f->version != g_wndVersion || !EqualRect(&r, &g_wndRect))
    {
        Present(*f, r);
        g_wndVersion = f->version;
        g_wndRect    = r;
    }
    const ULONGLONG now = Now();
    const HWND fg = GetForegroundWindow();
    if (!g_wndShown || !IsWindowVisible(g_wnd))
    {
        ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
        g_wndShown = true;
    }
    if (fg != g_lastFg || now - g_wndTop > 250)
    {
        SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        g_wndTop = now;
    }
    g_lastFg = fg;
}

void ShutdownScreenTranslate()
{
    g_shutdown = true;
    StopScreenTranslate();
    if (g_wnd)
    {
        DestroyWindow(g_wnd);
        g_wnd = nullptr;
    }
}
