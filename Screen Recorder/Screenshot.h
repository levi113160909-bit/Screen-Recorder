// =============================================================
//  Screenshot.h - لقطة شاشة (صورة PNG) من المشهد نفسه
//
//  بتاخد اللي عم يتسجّل/ينبث (المشهد بكل طبقاته) بالدقة الكاملة،
//  وبتحفظه PNG بمجلد "Screenshots" جوّا مجلد التسجيلات،
//  وبتنسخه للحافظة كمان (Ctrl+V بأي برنامج).
//  الشغل كله بخيط لحالو، فما بيعلّق الواجهة ولا اللعبة.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "Scene.h"

// لما تخلص: wParam = HGLOBAL صورة للحافظة (أو 0) ، lParam = new std::wstring(المسار) أو nullptr إذا فشلت
constexpr UINT WM_SCREENSHOT_DONE = WM_APP + 42;

void TakeScreenshot(Scene& scene, const std::wstring& folder, HWND notify);

// بيستلم الرسالة: بيحط الصورة بالحافظة وبيرجّع المسار (فاضي = فشلت)
std::wstring FinishScreenshot(HWND hwnd, WPARAM wParam, LPARAM lParam);
