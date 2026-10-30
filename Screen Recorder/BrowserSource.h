// =============================================================
//  BrowserSource.h - مصدر "صفحة ويب" (متل Browser Source تبع OBS)
//
//  تنبيهات المتابعين والتبرعات (Streamlabs ، StreamElements ...) وشات يوتيوب
//  كلها صفحات ويب. منفتحها بمتصفح Edge المدمج (WebView2) بلا نافذة،
//  ومنصوّرها بخلفية شفافة لحتى تنحط فوق اللعبة.
//
//  - في خيط واحد للمتصفح لكل الصفحات (WebView2 بدّو خيط STA مع حلقة رسائل)
//  - كل صفحة بترسم على "Visual" تبع Windows.UI.Composition ، ومنصوّر هالـ Visual
//    بـ Windows Graphics Capture (متل ما بيعمل GStreamer webview2src) - بلا نافذة ظاهرة
//  - الصفحة اللي ما حدا طلبها من 30 ثانية بتنسكّر لحالها
//  - الصوت (صوت التنبيه) بيطلع عالسماعات العادية فبيلتقطه "صوت الجهاز" بالتسجيل والبث
//  - الكوكيز محفوظة بـ %LOCALAPPDATA%\ScreenRecorder\WebView2
//    (فيك تفتح الصفحة بنافذة عادية وتسجّل دخول، وبعدين المصدر بيشوفك مسجّل)
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <memory>
#include <string>
#include "Media.h"

// هل متصفح WebView2 مثبّت؟ (موجود دايماً بويندوز 11، وبأغلب أجهزة ويندوز 10)
bool BrowserRuntimeAvailable();

// آخر صورة للصفحة (BGRA premultiplied بحجم width × height)
// missing = true إذا الصفحة ما انفتحت (لا إنترنت / رابط غلط / المتصفح مش مثبّت)
// nullptr بلا missing = لسا عم تحمّل
std::shared_ptr<const MediaFrame> BrowserFrame(const std::wstring& url, int width, int height, bool* missing);

// إعادة تحميل الصفحة (لو علّق التنبيه مثلاً)
void BrowserReload(const std::wstring& url, int width, int height);

// فتح الرابط بنافذة عادية للتفاعل (تسجيل دخول، إعدادات الويدجت...)
// لما تسكّرها، كل صفحات المصادر بتتحدّث لحتى تاخد الكوكيز الجديدة
void BrowserOpenInteractive(const std::wstring& url);

// يصحّح الرابط اللي كتبه المستخدم: بلا https:// منضيفها ، ومسار ملف بيصير file:///
std::wstring NormalizeUrl(const std::wstring& typed);

// اسم مختصر للطبقة من الرابط (مثلاً streamlabs.com)
std::wstring UrlDisplayName(const std::wstring& url);
