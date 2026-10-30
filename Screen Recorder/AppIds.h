// =============================================================
//  AppIds.h - أرقام عناصر الواجهة الرئيسية
//
//  كل زر أو عنصر بينكبس إلو رقم (AddHit). الكبسة بتوصل لـ OnClick بـ Commands.cpp.
//
//  توزيع الأرقام بكل البرنامج (ممنوع يتداخلوا، والمترجم بيتأكد لحالو تحت):
//      1 .. 999          أزرار وخيارات مفردة (هالملف: enum)
//      1000 .. 49999     قوائم (مشاهد، وجهات بث، أصوات برامج، طبقات): كتل IdBlock تحت
//      50000 .. 59999    النوافذ الداخلية (Panel.h: ID_PANEL_*)
//      60000 ..          عناصر الواجهة الرئيسية جوّا نافذة الإعدادات = الرقم الأصلي + SETTINGS_ID_OFFSET
//
//  لتضيف زر جديد: خد رقم فاضي تحت 1000، وضيفه لـ IdCheck::ALL بآخر الملف.
//  لتضيف خيارات (DrawSegmented): خد أول رقم، وسجّل عددها بـ IdCheck::ALL.
// =============================================================
#pragma once
#include "Ui.h"

// ---------- أزرار وخيارات مفردة (1 .. 999) ----------
enum : int
{
    ID_RECORD = 1, ID_PAUSE, ID_FOLDER, ID_ADD_SOURCE, ID_SYSTEM_AUDIO, ID_MIC,
    ID_SYSTEM_DEVICE, ID_MIC_DEVICE, ID_ENCODER_MENU,
    ID_TAB_SOURCES = 10, ID_TAB_STREAM, ID_TAB_SETTINGS,     // التبويبات (لازم ورا بعض)
    ID_BITRATE_MINUS, ID_BITRATE_PLUS, ID_ADD_SCENE,
    ID_SYSTEM_MUTE, ID_MIC_MUTE, ID_SYSTEM_VOL, ID_MIC_VOL,
    ID_STREAM, ID_ADD_TARGET, ID_STREAM_KBPS_MINUS, ID_STREAM_KBPS_PLUS,
    ID_REPLAY, ID_REPLAY_STOP, ID_MIC_FILTERS,                // 24 ، 25 ، 26

    ID_CANVAS_FIRST       = 90,   // 4 خيارات: الشاشة ، 1440p ، 1080p ، 720p
    ID_FPS_FIRST          = 100,  // 6: سطر الأرقام الثابتة 60 ، 75 ، 100 ، 120 ، 144 ، 165
    ID_QUALITY_FIRST      = 110,  // 4: منخفضة ، متوسطة ، عالية ، مخصّصة
    ID_FPS_MODE_FIRST     = 120,  // 3: تلقائي ، 30 ، مثل اللعبة
    ID_CODEC_FIRST        = 126,  // 3: H.264 ، HEVC ، AV1
    ID_CONTAINER_FIRST    = 130,  // 2: MP4 آمن ، MP4 عادي
    ID_STREAM_RES_FIRST   = 140,  // 3: دقة البث
    ID_REPLAY_SEC_FIRST   = 150,  // 4: مدة الإعادة (قديم: صارت بنافذة الإعادة، الأرقام محجوزة)
    ID_TRANSITION_FIRST   = 160,  // 3: الانتقال بين المشاهد
    ID_VERTICAL_FIRST     = 170,  // 2: طريقة البث العمودي
    ID_AUDIO_TRACKS_FIRST = 180,  // 2: مسار صوت واحد / مسارات منفصلة
    ID_OVERLAY_FIRST      = 190,  // 5: مؤشر التسجيل (مخفي + 4 زوايا)
    ID_HOTKEYS            = 200,  // زر نافذة الاختصارات
    ID_CLOSE_FIRST        = 210,  // 2: ✕ = جنب الساعة / إغلاق
    ID_STARTUP_FIRST      = 220,  // 2: التشغيل مع ويندوز
    ID_REPLAY_SETTINGS    = 230,  // فتح الإعدادات على صفحة الإعادة
    ID_OPEN_SETTINGS      = 240,  // زر الترس بالشريط العلوي
    ID_TOASTS_FIRST       = 245,  // 2: الإشعارات
    ID_TEST_TOAST         = 247,  // زر "تجربة"
    ID_OVERAPP_FIRST      = 248,  // 2: المؤشر فوق نافذة البرنامج كمان؟
    ID_OVERAPP_SECOND     = 249,  //    (الخيار التاني من نفس الزوج)
    ID_FOLDER_CHANGE      = 250,  // مجلد التسجيلات: تغيير
    ID_FOLDER_OPEN        = 251,  //                 فتح
    ID_STREAM_FPS_FIRST   = 260,  // 3: إطارات البث (مثل التسجيل ، 30 ، 60)
    ID_STREAM_PRESET_FIRST = 264, // 6: أرقام جاهزة لمعدّل بت البث (264..269)
    ID_STREAM_SETTINGS    = 270,  // زر "إعدادات البث" بتبويب البث
    ID_STREAM_COPY_FIRST  = 272,  // 2: حفظ نسخة من البث
    ID_STREAM_ADAPT_FIRST = 274,  // 2: البت ريت الذكي
    ID_THEME_FIRST        = 280,  // Theme::THEME_COUNT ثيمات
    ID_ACCENT_FIRST       = 284,  // Theme::ACCENT_COUNT ألوان + مخصّص (آخر واحد)
    ID_ABOUT_GITHUB       = 292,
    ID_OPEN_CONFIG        = 293,
    ID_CHECK_UPDATE       = 294,
    ID_GET_UPDATE         = 295,
    ID_AUTOGAME_FIRST     = 296,  // 2: كشف الألعاب
    ID_SETUP_AGAIN        = 298,  // إعادة الإعداد السريع
    ID_VCAM_TOGGLE        = 299,  // تشغيل/إيقاف الكاميرا الافتراضية (تبويب البث)
    ID_OPEN_CRASHES       = 300,  // فتح مجلد تقارير الأعطال (الإعدادات ← عام ← حول)
    ID_ADMIN_FIRST        = 302,  // 2: الصلاحية (عادية / كمسؤول) ، الإعدادات ← عام
};

// ---------- قوائم: كتلة أرقام لكل نوع ----------
// رقم العنصر = first + ترتيبه (أو رقم الطبقة). حجم الكتلة = أقصى عدد عناصر.
struct IdBlock
{
    int first, size;
    constexpr int  Id(int index) const    { return first + index; }
    constexpr bool Has(int id) const      { return id >= first && id < first + size; }
    constexpr bool Fits(int index) const  { return index >= 0 && index < size; }   // في رقم لهالعنصر؟
    constexpr int  Index(int id) const    { return id - first; }
    constexpr int  End() const            { return first + size; }
};

inline constexpr IdBlock IDB_SCENE         { 1000, 1000 };    // المشهد رقم i
inline constexpr IdBlock IDB_TARGET_TOGGLE { 2000, 500 };     // مفتاح وجهة البث رقم i
inline constexpr IdBlock IDB_TARGET_MENU   { 2500, 500 };     // قائمة وجهة البث رقم i
inline constexpr IdBlock IDB_APP_ROW       { 3000, 500 };     // صوت البرنامج رقم i (السطر)
inline constexpr IdBlock IDB_APP_MUTE      { 3500, 500 };     //   زر الكتم
inline constexpr IdBlock IDB_APP_DELETE    { 4000, 500 };     //   زر الإزالة
inline constexpr IdBlock IDB_APP_VOL       { 4500, 500 };     //   شريط العلو
// الطبقات: بالرقم الثابت تبع الطبقة (l.id) مش بالترتيب. الرقم بيكبر مع كل طبقة جديدة،
// فالكتلة كبيرة. طبقة رقمها أكبر من الحجم بتنرسم بس ما بتاخد رقم (ما بتنكبس غلط).
inline constexpr IdBlock IDB_LAYER         { 10000, 10000 };  // سطر الطبقة
inline constexpr IdBlock IDB_LAYER_EYE     { 20000, 10000 };  //   زر الإظهار/الإخفاء
inline constexpr IdBlock IDB_LAYER_DELETE  { 30000, 10000 };  //   زر الحذف

inline constexpr int ID_MAIN_LAST = 49999;                    // بعدها أرقام النوافذ الداخلية

// ---------- المترجم بيتأكد إنو ما في رقمين فوق بعض ----------
namespace IdCheck
{
    struct Range { int first, count; };
    inline constexpr Range ALL[] = {
        { ID_RECORD, ID_MIC_FILTERS - ID_RECORD + 1 },        // الأزرار المتسلسلة 1..26
        { ID_CANVAS_FIRST, 4 }, { ID_FPS_FIRST, 6 }, { ID_QUALITY_FIRST, 4 }, { ID_FPS_MODE_FIRST, 3 },
        { ID_CODEC_FIRST, 3 }, { ID_CONTAINER_FIRST, 2 }, { ID_STREAM_RES_FIRST, 3 }, { ID_REPLAY_SEC_FIRST, 4 },
        { ID_TRANSITION_FIRST, 3 }, { ID_VERTICAL_FIRST, 2 }, { ID_AUDIO_TRACKS_FIRST, 2 }, { ID_OVERLAY_FIRST, 5 },
        { ID_HOTKEYS, 1 }, { ID_CLOSE_FIRST, 2 }, { ID_STARTUP_FIRST, 2 }, { ID_REPLAY_SETTINGS, 1 },
        { ID_OPEN_SETTINGS, 1 }, { ID_TOASTS_FIRST, 2 }, { ID_TEST_TOAST, 1 }, { ID_OVERAPP_FIRST, 2 },
        { ID_FOLDER_CHANGE, 1 }, { ID_FOLDER_OPEN, 1 }, { ID_STREAM_FPS_FIRST, 3 }, { ID_STREAM_PRESET_FIRST, 6 },
        { ID_STREAM_SETTINGS, 1 }, { ID_STREAM_COPY_FIRST, 2 }, { ID_STREAM_ADAPT_FIRST, 2 },
        { ID_THEME_FIRST, Theme::THEME_COUNT }, { ID_ACCENT_FIRST, Theme::ACCENT_COUNT + 1 },
        { ID_ABOUT_GITHUB, 1 }, { ID_OPEN_CONFIG, 1 }, { ID_CHECK_UPDATE, 1 }, { ID_GET_UPDATE, 1 },
        { ID_AUTOGAME_FIRST, 2 }, { ID_SETUP_AGAIN, 1 }, { ID_VCAM_TOGGLE, 1 }, { ID_OPEN_CRASHES, 1 }, { ID_ADMIN_FIRST, 2 },
        { IDB_SCENE.first, IDB_SCENE.size }, { IDB_TARGET_TOGGLE.first, IDB_TARGET_TOGGLE.size },
        { IDB_TARGET_MENU.first, IDB_TARGET_MENU.size }, { IDB_APP_ROW.first, IDB_APP_ROW.size },
        { IDB_APP_MUTE.first, IDB_APP_MUTE.size }, { IDB_APP_DELETE.first, IDB_APP_DELETE.size },
        { IDB_APP_VOL.first, IDB_APP_VOL.size }, { IDB_LAYER.first, IDB_LAYER.size },
        { IDB_LAYER_EYE.first, IDB_LAYER_EYE.size }, { IDB_LAYER_DELETE.first, IDB_LAYER_DELETE.size },
    };
    constexpr bool NoOverlap()
    {
        for (const Range& a : ALL)
        {
            if (a.first < 1 || a.count < 1 || a.first + a.count - 1 > ID_MAIN_LAST)
                return false;
            for (const Range& b : ALL)
                if (&a != &b && a.first < b.first + b.count && b.first < a.first + a.count)
                    return false;
        }
        return true;
    }
    static_assert(NoOverlap(), "AppIds.h: رقمين عناصر فوق بعض أو برّا المجال 1..49999");
    static_assert(ID_TAB_STREAM == ID_TAB_SOURCES + 1 && ID_TAB_SETTINGS == ID_TAB_SOURCES + 2, "التبويبات لازم ورا بعض");
    static_assert(ID_OVERAPP_SECOND == ID_OVERAPP_FIRST + 1, "ID_OVERAPP_SECOND");
    static_assert(ID_MIC_FILTERS == 26 && ID_MIC_FILTERS < ID_CANVAS_FIRST, "الأزرار المتسلسلة وصلت لـ ID_CANVAS_FIRST");
}
