// =============================================================
//  VideoTools.h - أدوات على ملفات الفيديو الجاهزة
//
//   - مدة الفيديو
//   - صورة من الفيديو عند أي وقت (للصور المصغّرة ومعاينة القص)
//   - قصّ بدون إعادة ضغط: منقل القطع المضغوطة متل ما هي لملف جديد،
//     فبيخلص بثواني وبنفس الجودة تماماً. البداية بتصير عند أقرب
//     "إطار كامل" قبل النقطة اللي اخترتها (بفرق ثانيتين كحد أقصى).
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>

bool VideoDuration(const std::wstring& path, LONGLONG& duration100ns);

// صورة BGRA (من فوق لتحت) عرضها maxWidth كحد أقصى
bool VideoFrameAt(const std::wstring& path, LONGLONG time100ns, int maxWidth,
                  std::vector<BYTE>& pixels, int& width, int& height);

// القص: progress من 0 لـ 100 ، cancel = وقّف
HRESULT TrimVideo(const std::wstring& input, const std::wstring& output, LONGLONG start100ns, LONGLONG end100ns,
                  std::atomic<int>* progress = nullptr, std::atomic<bool>* cancel = nullptr);

// صورة مصغّرة من ويندوز نفسه (للفيديو والصور) ، وإذا ما في: من الفيديو
bool ShellThumbnail(const std::wstring& path, int size, std::vector<BYTE>& pixels, int& width, int& height);
