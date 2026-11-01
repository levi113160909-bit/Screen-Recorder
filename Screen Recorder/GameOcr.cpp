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
