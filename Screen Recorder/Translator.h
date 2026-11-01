// =============================================================
//  Translator.h - ترجمة الألعاب للعربي (مباشرة على البث)
//
//  الفكرة:
//   1) منقرا مكان الترجمة الإنكليزية بالصورة (جهاز الالتقاط أو جزء من الشاشة)
//      بقارئ النصوص تبع ويندوز (GameOcr) ، كذا مرة بالثانية
//   2) كل جملة جديدة منبعتها لخدمة ترجمة (ذكاء صناعي أو ترجمة آلية أو على جهازك)
//      والجمل المكررة منحفظها لحتى ما تنترجم مرتين
//   3) طبقة "ترجمة اللعبة" بالمشهد بترسم مربع غامق فوق الإنكليزي وفيه العربي
//   4) التأخير: منأخّر صورة اللعبة وصوتها (وإذا بدك المايك والكاميرا) شوي،
//      فالمشاهد بيشوف العربي من أول لحظة بتطلع فيها الجملة
//
//  "ملف شخصي" لكل لعبة: مكان الترجمة ، خدمة الترجمة ، اللهجة ، الأسماء ، الشكل.
//  كل شي بينحفظ بـ %LOCALAPPDATA%\ScreenRecorder\translation.txt
//  (مفاتيح الخدمات مشفّرة بحساب ويندوز تبعك)
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
#include "Media.h"

// ---------- خدمات الترجمة ----------
enum class TransEngine { Gemini, Claude, OpenAI, DeepL, Azure, Google, Ollama, Count };
constexpr int TRANS_ENGINE_COUNT = (int)TransEngine::Count;

const wchar_t* EngineName(TransEngine e);        // "Gemini (Google)"
const wchar_t* EngineNote(TransEngine e);        // سطر صغير: مجاني؟ دقيق؟
bool           EngineIsAi(TransEngine e);        // بيفهم السياق واللهجة والأسماء
bool           EngineSeesImages(TransEngine e);  // بيقرا الصور (Gemini ، Claude ، ChatGPT)
bool           EngineNeedsKey(TransEngine e);
bool           EngineHasModel(TransEngine e);
bool           EngineHasExtra(TransEngine e);    // Azure: المنطقة ، Ollama: العنوان
const wchar_t* EngineExtraLabel(TransEngine e);
const wchar_t* EngineKeyHelp(TransEngine e);     // من وين بتجيب المفتاح
std::wstring   EngineDefaultModel(TransEngine e);

struct EngineConfig
{
    std::wstring key;       // المفتاح (بينحفظ مشفّر)
    std::wstring model;     // اسم النموذج (فاضي = الافتراضي)
    std::wstring extra;     // Azure: المنطقة (مثلاً westeurope) ، Ollama: العنوان
};

// ---------- اللهجة ----------
enum class TransDialect { Fusha, Levantine, Gulf, Egyptian, Iraqi, Count };
constexpr int TRANS_DIALECT_COUNT = (int)TransDialect::Count;
const wchar_t* DialectName(TransDialect d);

// اسم أو مصطلح: ar فاضي = بيضل متل ما هو (بالإنكليزي)
struct GlossaryItem
{
    std::wstring en, ar;
};

// ألوان النص
constexpr int TRANS_COLOR_COUNT = 3;
const wchar_t* TransColorName(int i);
unsigned       TransColor(int i);               // 0xRRGGBB

struct TranslateProfile
{
    std::wstring id;
    std::wstring name = L"ملف جديد";

    // المصدر: جهاز التقاط/كاميرا ، أو جزء من الشاشة (ألعاب الكمبيوتر)
    bool         fromScreen = false;
    std::wstring deviceLink, deviceName;
    RECT         monitor{};
    // مكان الترجمة الإنكليزية (نسبة من الصورة 0..1)
    double       rx = 0.15, ry = 0.80, rw = 0.70, rh = 0.15;

    // الترجمة
    TransEngine  engine  = TransEngine::Gemini;
    TransDialect dialect = TransDialect::Fusha;
    std::wstring game;                 // اسم اللعبة (بيساعد الذكاء الصناعي يفهم السياق)
    bool         soften = true;        // تلطيف الألفاظ القوية
    std::vector<GlossaryItem> glossary;

    // المزامنة: تأخير الصورة والصوت (ملّي ثانية)
    int          delayMs  = 1500;
    bool         delayAll = true;      // المايك والكاميرا وباقي الأصوات كمان (لحتى ما يسبق كلامك اللعبة)

    // القراءة: فصل النص الفاتح عن الخلفية (0 = بلا ، 120..250 = الحد)
    int          threshold = 0;
    // انتظار اكتمال الجملة: منترجم بس لما النص يوقف عن التغيّر هالقد (ملّي ثانية)
    // (ألعاب بتكتب الكلام حرف حرف). وإذا كبرت بعد الترجمة: retranslate = منرجع نترجمها كاملة
    int          settleMs = 600;
    bool         retranslate = true;
    // قراءة الصورة بالذكاء الصناعي: منبعت صورة مكان الترجمة نفسها للخدمة ، فبتقراها وبتترجمها مع بعض
    // (أدق بكتير من قارئ ويندوز مع الخطوط الصغيرة والمزخرفة). قارئ ويندوز بيضل بس ليعرف إمتى طلعت جملة جديدة
    // متاحة مع Gemini و Claude و ChatGPT
    bool         visionRead = true;

    // الشكل
    float        fontScale  = 1.0f;
    float        boxOpacity = 0.88f;
    int          colorIndex = 0;

    // الترجمة فوق اللعبة إلك إنت (نافذة شفافة فوق كل شي ، ما بتطلع بالبث ولا بالتسجيل)
    bool         playerOverlay = false;
    RECT         overlayMonitor{};     // لأجهزة الالتقاط: الشاشة اللي عليها صورة اللعبة (فاضي = الرئيسية)
};

// ---------- الملفات الشخصية والمفاتيح ----------
std::vector<TranslateProfile> TranslateProfiles();
bool         GetTranslateProfile(const std::wstring& id, TranslateProfile& out);
void         SaveTranslateProfile(const TranslateProfile& p);     // جديد أو تعديل (وبيحفظ بالملف)
// تعديل فوري (مثلاً وقت سحب منزلق). persist = احفظ بالملف كمان
void         SetTranslateProfile(const TranslateProfile& p, bool persist);
void         DeleteTranslateProfile(const std::wstring& id);
std::wstring NewTranslateProfileId();
EngineConfig GetEngineConfig(TransEngine e);
void         SetEngineConfig(TransEngine e, const EngineConfig& c);
void         ClearTranslateCache(const std::wstring& profileId);   // نسيان الترجمات المحفوظة

// ---------- أدوات للنافذة (بتوقّف الخيط: نادِها من خيط بالخلفية) ----------
bool TranslateOnce(const TranslateProfile& p, const std::wstring& english, std::wstring& arabic,
                   std::wstring& error, int* ms = nullptr);
std::vector<std::wstring> OllamaModels(const std::wstring& address, std::wstring* error = nullptr);

// ---------- الجلسة الحيّة ----------
struct TranslateStatus
{
    bool         running = false;
    bool         ocrReady = false, ocrEnglish = false;
    bool         sourceOk = false;          // الصورة واصلة؟
    std::wstring ocrLanguage;
    std::wstring lastEnglish, lastArabic;   // آخر جملة
    std::wstring error;                     // آخر خطأ من خدمة الترجمة
    int          translated = 0, fromCache = 0, lastMs = 0;
};
TranslateStatus GetTranslateStatus(const std::wstring& profileId);

// النافذة مفتوحة: منشغّل القراءة حتى لو الطبقة مش بالمشهد (لحتى تشوف النتيجة)
void KeepTranslateSessionAlive(const std::wstring& profileId);

// صورة طبقة "ترجمة اللعبة" (شفافة إذا ما في جملة هلق)
std::shared_ptr<const MediaFrame> TranslationLayerFrame(const SceneLayer& layer, bool* missing);

// نص تجريبي مكان الترجمة (وقت ترتيب المشهد ، مش وقت البث)
void SetTranslatePlaceholder(bool on);

// صورة المصدر الحيّة (للنافذة: لتحدّد مكان الترجمة عليها)
// الشاشة: بتنصوّر بخيط بالخلفية كذا مرة بالثانية (مصغّرة) ، فالنداء سريع وما بيعلّق الواجهة
std::shared_ptr<const MediaFrame> TranslateSourceFrame(const TranslateProfile& p);

// الجملة اللي ظاهرة هلق (بلا تأخير) ، مترجمة: لعرضها إلك فوق اللعبة
// (بتخلّي القراءة شغّالة طول ما عم تنادِها)
bool TranslateLiveLine(const std::wstring& profileId, std::wstring& arabic);

// صورة مربع الترجمة بهالحجم (BGRA مضروبة بالشفافية ، متل اللي بالبث). محفوظة: لا تعدّلها
std::shared_ptr<const MediaFrame> RenderTranslationBox(const std::wstring& text, int width, int height,
                                                       const TranslateProfile& p, bool dim);

// ---------- ترجمة الشاشة كلها (ScreenTranslate) ----------
struct ScreenTextItem
{
    std::wstring en, ar;      // ar فاضي = ما انترجم
};
// كل نصوص الشاشة بطلب واحد (والمترجم من قبل من الذاكرة). بتوقّف الخيط: نادِها من خيط بالخلفية
bool TranslateScreenTexts(const TranslateProfile& p, std::vector<ScreenTextItem>& items, std::wstring& error);
// صورة جزء من الشاشة (maxWidth > 0: مصغّرة لهالعرض). النوافذ المخفية عن التصوير ما بتطلع فيها
bool GrabScreenArea(const RECT& r, std::vector<uint8_t>& bgra, int& width, int& height, int maxWidth);

// البرنامج عم يسكّر: وقّف كل الخيوط
void ShutdownTranslator();

// المساحة اللي بياخدها التأخير بالذاكرة (ميغابايت تقريباً) لصورة بهالحجم
int DelayMemoryMb(int width, int height, double fps, int delayMs);
