// =============================================================
//  GameOcr.h - قراءة النص من الصورة (OCR) بقارئ ويندوز نفسه
//
//  ويندوز 10 و 11 فيهم قارئ نصوص مجاني بيشتغل بدون إنترنت
//  (Windows.Media.Ocr). منعطيه صورة مكان الترجمة ، وبيرجّع الأسطر.
//  بيحتاج لغة إنكليزية مثبّتة بويندوز (غالباً موجودة). إذا مش موجودة
//  منستعمل لغة الجهاز (بتقرا الحروف اللاتينية بس أضعف).
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class OcrReader
{
public:
    OcrReader();                     // لازم بخيط شغّال فيه COM (MTA)
    ~OcrReader();
    OcrReader(const OcrReader&) = delete;
    OcrReader& operator=(const OcrReader&) = delete;

    bool         Ready() const;      // في قارئ؟
    bool         English() const;    // القارئ إنكليزي؟ (الأفضل)
    std::wstring LanguageName() const;

    // صورة BGRA (من فوق لتحت). بيرجّع الأسطر من فوق لتحت
    bool Read(const uint8_t* bgra, int width, int height, int stride, std::vector<std::wstring>& lines);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

// فحص سريع (للواجهة): في قارئ إنكليزي بالجهاز؟
bool OcrHasEnglish();
