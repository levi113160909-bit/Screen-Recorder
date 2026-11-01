// =============================================================
//  GameOcr.cpp - قراءة النص من الصورة (شوف GameOcr.h)
// =============================================================
#include "GameOcr.h"
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "windowsapp.lib")

namespace wmo = winrt::Windows::Media::Ocr;
namespace wgi = winrt::Windows::Graphics::Imaging;
namespace wgl = winrt::Windows::Globalization;
namespace wss = winrt::Windows::Storage::Streams;

namespace
{
    const wchar_t* ENGLISH_TAGS[] = { L"en-US", L"en-GB", L"en" };
}

struct OcrReader::Impl
{
    wmo::OcrEngine engine{ nullptr };
    bool           english = false;
    std::wstring   language;
};

OcrReader::OcrReader() : m(std::make_unique<Impl>())
{
    try
    {
        for (const wchar_t* tag : ENGLISH_TAGS)
        {
            wgl::Language lang(tag);
            if (!wmo::OcrEngine::IsLanguageSupported(lang))
                continue;
            m->engine = wmo::OcrEngine::TryCreateFromLanguage(lang);
            if (m->engine)
            {
                m->english  = true;
                m->language = lang.DisplayName().c_str();
                return;
            }
        }
        m->engine = wmo::OcrEngine::TryCreateFromUserProfileLanguages();
        if (m->engine)
            m->language = m->engine.RecognizerLanguage().DisplayName().c_str();
    }
    catch (...)
    {
        m->engine = nullptr;
    }
}

OcrReader::~OcrReader() = default;

bool OcrReader::Ready() const          { return m->engine != nullptr; }
bool OcrReader::English() const        { return m->english; }
std::wstring OcrReader::LanguageName() const { return m->language; }

bool OcrReader::Read(const uint8_t* bgra, int width, int height, int stride, std::vector<std::wstring>& lines)
{
    lines.clear();
    if (!m->engine || !bgra || width < 8 || height < 8)
        return false;
    try
    {
        // القارئ إله حد أقصى للحجم
        const int maxDim = (int)wmo::OcrEngine::MaxImageDimension();
        if (width > maxDim || height > maxDim)
            return false;
        wgi::SoftwareBitmap bmp(wgi::BitmapPixelFormat::Bgra8, width, height, wgi::BitmapAlphaMode::Premultiplied);
        const uint32_t size = (uint32_t)width * height * 4;
        wss::Buffer buf(size);
        uint8_t* dst = buf.data();
        for (int y = 0; y < height; ++y)
            memcpy(dst + (size_t)y * width * 4, bgra + (size_t)y * stride, (size_t)width * 4);
        buf.Length(size);
        bmp.CopyFromBuffer(buf);

        const wmo::OcrResult result = m->engine.RecognizeAsync(bmp).get();
        for (const wmo::OcrLine& line : result.Lines())
        {
            std::wstring t = line.Text().c_str();
            if (!t.empty())
                lines.push_back(std::move(t));
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool OcrReader::ReadLines(const uint8_t* bgra, int width, int height, int stride, std::vector<OcrLineBox>& lines)
{
    lines.clear();
    if (!m->engine || !bgra || width < 8 || height < 8)
        return false;
    try
    {
        // أكبر من حد القارئ (مثلاً 4K): منصغّرها بعدد صحيح ، ومنرجّع الأماكن للحجم الأصلي
        const int maxDim = (int)wmo::OcrEngine::MaxImageDimension();
        const int scale = std::max(1, (std::max(width, height) + maxDim - 1) / std::max(1, maxDim));
        const int w = width / scale, h = height / scale;
        wgi::SoftwareBitmap bmp(wgi::BitmapPixelFormat::Bgra8, w, h, wgi::BitmapAlphaMode::Premultiplied);
        const uint32_t size = (uint32_t)w * h * 4;
        wss::Buffer buf(size);
        uint8_t* dst = buf.data();
        if (scale == 1)
        {
            for (int y = 0; y < h; ++y)
                memcpy(dst + (size_t)y * w * 4, bgra + (size_t)y * stride, (size_t)w * 4);
        }
        else
        {
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    memcpy(dst + ((size_t)y * w + x) * 4, bgra + (size_t)y * scale * stride + (size_t)x * scale * 4, 4);
        }
        buf.Length(size);
        bmp.CopyFromBuffer(buf);

        const wmo::OcrResult result = m->engine.RecognizeAsync(bmp).get();
        for (const wmo::OcrLine& line : result.Lines())
        {
            // القارئ أحياناً بيجمع عناصر بعيدة عن بعض بسطر واحد (متل قائمة بالعرض: "Items   Map   Quests"):
            // فراغ كبير بين كلمتين = عنصر جديد
            float lineH = 0;
            for (const wmo::OcrWord& word : line.Words())
                lineH = std::max(lineH, word.BoundingRect().Height);
            OcrLineBox b;
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            auto flush = [&]
            {
                if (!b.text.empty() && x1 > x0 && y1 > y0)
                {
                    b.x = x0 * scale;
                    b.y = y0 * scale;
                    b.w = (x1 - x0) * scale;
                    b.h = (y1 - y0) * scale;
                    lines.push_back(b);
                }
                b = OcrLineBox{};
                x0 = y0 = 1e9f;
                x1 = y1 = -1e9f;
            };
            for (const wmo::OcrWord& word : line.Words())
            {
                const winrt::Windows::Foundation::Rect r = word.BoundingRect();
                if (!b.text.empty() && r.X - x1 > std::max(12.0f, lineH * 1.8f))
                    flush();
                if (!b.text.empty()) b.text += L' ';
                b.text += word.Text().c_str();
                x0 = std::min(x0, r.X);
                y0 = std::min(y0, r.Y);
                x1 = std::max(x1, r.X + r.Width);
                y1 = std::max(y1, r.Y + r.Height);
            }
            flush();
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool OcrHasEnglish()
{
    try
    {
        for (const wchar_t* tag : ENGLISH_TAGS)
            if (wmo::OcrEngine::IsLanguageSupported(wgl::Language(tag)))
                return true;
    }
    catch (...) {}
    return false;
}
