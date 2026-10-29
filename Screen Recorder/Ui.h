// =============================================================
//  Ui.h - أدوات الواجهة المشتركة
//
//  هون كل شي بيحتاجه أي جزء من الواجهة:
//   - الألوان (الثيم الغامق) والأيقونات
//   - أدوات الرسم الصغيرة (مستطيل، دائرة، نص...)
//   - تسجيل الأزرار لحتى نعرف شو انكبس
//   - عناصر جاهزة: مفتاح تشغيل، أزرار متلاصقة، مقياس صوت، زر
//
//  أي نافذة أو ميزة جديدة بتعمل #include "Ui.h" وبتصير
//  بنفس شكل البرنامج تلقائياً.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// =============================================================
//  الألوان (ثيم داكن)
// =============================================================
namespace Theme
{
    inline const D2D1_COLOR_F Background = D2D1::ColorF(0x0E1014);
    inline const D2D1_COLOR_F Panel      = D2D1::ColorF(0x16191F);
    inline const D2D1_COLOR_F Card       = D2D1::ColorF(0x1D2129);
    inline const D2D1_COLOR_F Hover      = D2D1::ColorF(0x252A34);
    inline const D2D1_COLOR_F Border     = D2D1::ColorF(0x2A2F3A);
    inline const D2D1_COLOR_F Text       = D2D1::ColorF(0xE8EAED);
    inline const D2D1_COLOR_F SubText    = D2D1::ColorF(0x8B93A3);
    inline const D2D1_COLOR_F Accent     = D2D1::ColorF(0x4F8CFF);
    inline const D2D1_COLOR_F Red        = D2D1::ColorF(0xE5484D);
    inline const D2D1_COLOR_F RedHover   = D2D1::ColorF(0xF0656A);
    inline const D2D1_COLOR_F Green      = D2D1::ColorF(0x3DD68C);
    inline const D2D1_COLOR_F Yellow     = D2D1::ColorF(0xF5A524);
    inline const D2D1_COLOR_F Off        = D2D1::ColorF(0x3A404D);
    inline const D2D1_COLOR_F White      = D2D1::ColorF(0xFFFFFF);
}

// أيقونات من خط "Segoe MDL2 Assets" (موجود بكل ويندوز 10 و 11)
namespace Icon
{
    inline const wchar_t* Monitor = L"\uE7F4";
    inline const wchar_t* Window  = L"\uE8A7";
    inline const wchar_t* Speaker = L"\uE767";
    inline const wchar_t* Mic     = L"\uE720";
    inline const wchar_t* Muted   = L"\uE74F";
    inline const wchar_t* Pause   = L"\uE769";
    inline const wchar_t* Play    = L"\uE768";
    inline const wchar_t* Folder  = L"\uE838";
    inline const wchar_t* Add     = L"\uE710";
    inline const wchar_t* Delete  = L"\uE74D";
    inline const wchar_t* Eye     = L"\uE890";
    inline const wchar_t* Chevron = L"\uE70D";
    inline const wchar_t* Live    = L"\uE93E";
    inline const wchar_t* More    = L"\uE712";
    inline const wchar_t* Camera  = L"\uE722";
    inline const wchar_t* Photo   = L"\uEB9F";
    inline const wchar_t* Font    = L"\uE8D2";
    inline const wchar_t* Replay  = L"\uE81C";
    inline const wchar_t* Filter  = L"\uE9E9";
    inline const wchar_t* Close   = L"\uE711";
    // أيقونات جديدة: منكتبها كأرقام لحتى ما تتخرّب وقت النسخ
    inline const wchar_t Refresh[] = { 0xE72C, 0 };   // تحديث / إعادة
    inline const wchar_t Undo[]    = { 0xE7A7, 0 };   // رجوع للافتراضي
    inline const wchar_t Keyboard[] = { 0xE765, 0 };  // لوحة المفاتيح
}

// =============================================================
//  أدوات الرسم المشتركة (معرّفة بـ Ui.cpp)
// =============================================================
extern ComPtr<IDWriteFactory>        g_dwrite;
extern ComPtr<ID2D1HwndRenderTarget> g_rt;
extern ComPtr<ID2D1SolidColorBrush>  g_brush;
extern ComPtr<IDWriteTextFormat>     g_fTitle, g_fSection, g_fBody, g_fSmall, g_fSmallEnd, g_fSmallLtr, g_fCenter, g_fSegment,
                                     g_fTimer, g_fIcon, g_fIconBig;

// الماوس والأزرار: وقت الرسم كل زر بيسجّل مكانه ورقمه
struct Hit { D2D1_RECT_F rect; int id; };
extern std::vector<Hit> g_hits;        // وين كل زر بآخر رسمة
extern D2D1_POINT_2F    g_mouse;       // مكان الماوس (بالنقاط)

// منطقة الكبس المسموحة (للقوائم اللي بتتمرّر: ما منخلّي شي مخفي ينكبس)
extern bool        g_hitClipOn;
extern D2D1_RECT_F g_hitClip;

// ---------- أدوات رسم صغيرة ----------
bool         Inside(const D2D1_RECT_F& r, D2D1_POINT_2F p);
D2D1_RECT_F  Rect(float l, float t, float r, float b);
void         Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0);
void         Stroke(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius, float width = 1);
void         Circle(float cx, float cy, float radius, const D2D1_COLOR_F& c);
void         Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float width = 1);
void         Text(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a);

// بيسجّل زر، وبيرجّع إذا الماوس فوقه
bool AddHit(const D2D1_RECT_F& r, int id);
// رقم الزر اللي تحت الماوس (آخر واحد انرسم = الأعلى)
int  HitAt(D2D1_POINT_2F p);

// ---------- الخطوط ----------
ComPtr<IDWriteTextFormat> MakeFormat(float size, DWRITE_FONT_WEIGHT weight, bool rtl,
                                     DWRITE_TEXT_ALIGNMENT align, const wchar_t* family = L"Segoe UI");
void  CreateTextFormats();
float TextWidth(const std::wstring& s, IDWriteTextFormat* f);

// ---------- عناصر جاهزة ----------
void DrawToggle(float x, float y, bool on, bool enabled);                 // مفتاح تشغيل/إطفاء (42×24)
void DrawMeter(const D2D1_RECT_F& r, float level, bool active);           // شريط مستوى صوت (0..1)
void DrawSegmented(const D2D1_RECT_F& r, const std::vector<std::wstring>& labels, int selected, int firstId, bool enabled);
// زر عادي: primary = أزرق. بيرجّع إذا الماوس فوقه
bool DrawButton(const D2D1_RECT_F& r, const std::wstring& label, int id, bool primary = false, bool enabled = true,
                const wchar_t* icon = nullptr);

// ---------- أرقام ونصوص ----------
// من قوة الصوت (0..1) لطول المقياس (0..1)، من -60 لـ 0 ديسيبل
float        PeakToMeter(float peak);
// رقم بعلامات لحتى ما ينقلب بنص عربي (مثلاً "-42")
std::wstring LtrText(const std::wstring& s);
