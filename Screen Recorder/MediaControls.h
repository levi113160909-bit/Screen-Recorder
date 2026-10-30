// =============================================================
//  MediaControls.h - أزرار المشغّل المشتركة (المشغّل ونافذة القص)
//
//   - زر قفز ±ثواني: دائرة، وجوّاها سهم ملتف حول الرقم
//     (+ = مع عقارب الساعة ، - = عكسها)
//   - زر تشغيل/إيقاف كبير
//   - نقل صورة المشغّل الجديدة لـ Direct2D
// =============================================================
#pragma once
#include "Ui.h"
#include "VideoPlayer.h"

// seconds موجبة = تقديم ، سالبة = رجوع
void DrawJumpButton(float cx, float cy, float radius, int seconds, bool hover, bool enabled);
void DrawPlayButton(float cx, float cy, float radius, bool playing, bool hover, bool enabled);

// صورة جديدة من المشغّل؟ بتحدّث bmp (وبتعيد بناءها إذا انمسحت). true = في صورة للرسم
bool UpdatePlayerBitmap(VideoPlayer& player, int maxWidth, std::vector<BYTE>& px, int& w, int& h,
                        ComPtr<ID2D1Bitmap>& bmp);

// وقت مقروء: 1:05 أو 1:02:03 (tenths = عشر الثانية)
std::wstring MediaClock(LONGLONG t100ns, bool tenths = false);
