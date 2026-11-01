// =============================================================
//  ScreenTranslate.h - "ترجمة الشاشة كلها" بكبسة زر (متل Google Lens للألعاب)
//
//  بتكبس الاختصار:
//   1) منصوّر الشاشة اللي قدامك (أو صورة جهاز الالتقاط) مرة وحدة
//   2) منقرا كل النصوص الإنكليزية مع أماكنها (القوائم ، الأزرار ، المهمات ، الرسائل)
//      والأسطر اللي ورا بعض بنفس الفقرة منجمعها (لحتى تنترجم كجملة وحدة)
//   3) منترجمهم كلهم بطلب واحد (والمترجم من قبل من الذاكرة: القوائم بتتكرر)
//   4) فوق كل نص: مربع غامق فيه العربي ، بنفس مكانه وحجمه
//      - إلك: نافذة شفافة فوق الشاشة (ما بتطلع بالتصوير ، والماوس بيمرق من خلالها)
//      - للجمهور: طبقة مؤقتة بالمشهد فوق طبقة المصدر (مع نفس تأخير البث)
//   5) بتختفي: لما تكبس مرة تانية ، أو لما تتغيّر الشاشة (سكّرت القائمة مثلاً)
//
//  الإعدادات (خدمة الترجمة ، اللهجة ، الأسماء ، اللون) من ملف الترجمة الشخصي
// =============================================================
#pragma once
#include "Translator.h"

struct ScreenTranslateInfo
{
    bool         working = false;      // عم يقرا ويترجم
    bool         showing = false;      // الترجمة ظاهرة إلك
    bool         streamLayer = false;  // لازم طبقة بالبث (ظاهرة ، أو لسا التأخير ما خلص بعد الإخفاء)
    bool         fromScreen = true;
    RECT         monitor{};            // المصدر: الشاشة
    std::wstring deviceLink;           // المصدر: جهاز الالتقاط
    std::wstring profileId;
    uint64_t     job = 0;              // بيتغيّر مع كل كبسة
    bool         finished = false;     // المحاولة خلصت (نجحت أو لا)
    int          count = 0;            // كم نص انترجم
    std::wstring error;
};

// fromScreen: الشاشة monitor ، وإلا صورة جهاز الالتقاط (p.deviceLink)
// playerMonitor: الشاشة اللي بتطلع عليها الترجمة إلك (مع الجهاز: الشاشة اللي عليها صورة اللعبة)
void StartScreenTranslate(const TranslateProfile& p, bool fromScreen, const RECT& monitor, const RECT& playerMonitor);
void StopScreenTranslate();
ScreenTranslateInfo GetScreenTranslateInfo();

// من مؤقّت الإطار: نافذتك فوق الشاشة
void UpdateScreenTranslate();

// صورة طبقة البث المؤقتة (شفافة إذا مش وقتها) - بحجم صورة المصدر
std::shared_ptr<const MediaFrame> ScreenTranslateLayerFrame();

// مصدر طبقة البث المؤقتة (SceneLayer::source)
inline constexpr wchar_t SCREEN_TRANSLATE_SOURCE[] = L"screen-translate";

void ShutdownScreenTranslate();
