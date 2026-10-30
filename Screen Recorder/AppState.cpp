// =============================================================
//  AppState.cpp - حالة البرنامج المشتركة: المتغيرات اللي بتستعملها أكتر من ميزة
// =============================================================
#include "App.h"
#include "Version.h"
#include <string_view>

// رقم الإصدار: النص لازم يطابق الأرقام (Version.h)
#define APP_STRINGIZE2(x) #x
#define APP_STRINGIZE(x)  APP_STRINGIZE2(x)
static_assert(std::string_view(APP_VERSION_STR) ==
              APP_STRINGIZE(APP_VER_MAJOR) "." APP_STRINGIZE(APP_VER_MINOR) "." APP_STRINGIZE(APP_VER_PATCH),
              "Version.h: APP_VERSION_STR ما بيطابق الأرقام");

HotkeyManager g_hotkeys;     // الاختصارات (بتتغيّر من نافذة "اختصارات لوحة المفاتيح")

HWND  g_hwnd = nullptr;

UINT  g_dpi  = 96;

// أدوات الرسم (الألوان والخطوط وأدوات الرسم الصغيرة صاروا بـ Ui.h)
ComPtr<ID2D1Factory>          g_d2d;

float            g_scroll = 0;         // تمرير قائمة الطبقات

float            g_scrollMax = 0;      // أقصى تمرير للتبويب الحالي (لحتى العجلة توقف عند النهاية)

D2D1_RECT_F      g_listRect{};

// المشهد اللي شغّال هلق (هو اللي بيتسجّل وبيبيّن بالمعاينة)
Scene g_scene;

std::vector<SceneData> g_scenes;

int                    g_activeScene = 0;

int   g_selectedLayer = 0;             // رقم الطبقة المختارة (0 = ولا وحدة)

Drag          g_drag = Drag::None;

int           g_dragHandle = -1;       // أي مقبض (0..7) عم نسحب

std::vector<double> g_guidesX, g_guidesY;   // خطوط المغناطيس اللي بتبيّن وقت السحب (ببكسل اللوحة)

// الصوت
AudioSource g_systemAudio(AudioSource::Kind::System);

AudioSource g_micAudio(AudioSource::Kind::Microphone);

bool  g_systemOn = true, g_micOn = false;

AudioSource* g_volDrag = nullptr;      // شريط العلو اللي عم نسحبه بالماوس

D2D1_RECT_F  g_volTrack[2] = {};       // مكان شريط العلو (0 = النظام ، 1 = الميكروفون)

float g_systemLevel = 0, g_micLevel = 0;

// أصوات البرامج (كل مشهد إله برامجه، شوف AppAudio.h)
AppAudioMixer            g_appMixer;

std::vector<float>       g_appLevels;          // مستوى صوت كل برنامج (للمقياس)

std::vector<D2D1_RECT_F> g_appTracks;          // مكان شريط علو كل برنامج بآخر رسمة

int                      g_appVolDrag = -1;    // شريط البرنامج اللي عم نسحبه (-1 = ولا واحد)

// البث المباشر
Streamer                  g_streamer;

std::vector<StreamTarget> g_targets;            // كل الوجهات (المفعّلة والمطفية)

std::vector<int>          g_liveTargets;        // أرقام الوجهات اللي عم نبث عليها هلق

std::vector<OutputStatus> g_streamStatus;       // حالة كل وجهة شغّالة

int                       g_streamRes  = 1;     // 0 = بحجم اللوحة ، 1 = 1080p ، 2 = 720p

UINT32                    g_streamKbps = 6000;  // معدّل البت للبث (كيلوبت/ث) - منفصل عن التسجيل

int                       g_streamFpsChoice = 0;

bool                      g_streamSaveCopy  = true;   // نسخة من البث على الجهاز

bool                      g_streamAdaptive  = true;   // خفّض المعدّل لحاله إذا ضعف النت // إطارات البث: 0 مثل التسجيل (حد 60) ، 1 = 30 ، 2 = 60

int                       g_verticalMode = 0;   // البث العمودي: 0 قصّ المنتصف ، 1 الشاشة كاملة

// الإعادة (آخر ثواني بالذاكرة)
Streamer                      g_replay;

std::shared_ptr<ReplayBuffer> g_replayBuffer = std::make_shared<ReplayBuffer>();

bool                          g_replayWanted = false;      // شغّالة هلق؟ (للحفظ)

ReplaySettings                g_replaySet;                 // إعدادات الإعادة (نافذة "إعدادات الإعادة")

HMONITOR                      g_replayMonitorHandle = nullptr;

// كشف الألعاب: اللعبة اللي شغّالة هلق (الإعادة بتلتقط نافذتها)
bool                          g_autoGame = true;

HWND                          g_gameHwnd = nullptr;

CaptureTarget                 g_gameTarget;

bool                          g_replayByGame = false;      // الإعادة اشتغلت لحالها بسبب اللعبة

bool                          g_firstRunDone = false;      // انعرض الإعداد السريع؟

// الانتقال بين المشاهد
int       g_transitionChoice = 1;                          // 0 فوري ، 1 تلاشي سريع ، 2 تلاشي ناعم

int       g_fadeTicks = 0;                                 // وقت التلاشي: منحدّث المعاينة أسرع

// التسجيل
Recorder     g_recorder;

std::wstring g_folder, g_lastFile, g_message;

bool         g_messageIsError = false;

// المعاينة
Compositor            g_previewCompositor(false);   // false = بلا ماوس بالمعاينة

ComPtr<ID2D1Bitmap>   g_previewBitmap;

D2D1_RECT_F           g_previewArea{};     // المساحة المتاحة للمعاينة

bool                  g_previewValid = false;

// أيقونات البرنامج (شريط المهام)
HICON g_icons[3][2] = {};              // [عادي / عم يسجّل / متوقف مؤقتاً][كبيرة / صغيرة]

int   g_iconState = -1;

int g_tick = 0;

int g_replaySavedTick = -1000;       // إمتى آخر مرة حفظنا إعادة (للمؤشر)

int g_tab  = 0;      // 0 = المصادر ، 1 = البث ، 2 = الإعدادات

int          g_canvasChoice   = 0;

int          g_fpsChoice      = 0;

int          g_quality        = 2;     // 0 منخفضة ، 1 متوسطة ، 2 عالية ، 3 مخصّصة

int          g_encoderMode    = 0;     // 0 تلقائي ، 1 بطاقة الرسومات ، 2 المعالج

bool         g_crashSafe      = true;  // MP4 آمن (مقطّع)

bool         g_separateTracks = false; // مسار صوت لكل مصدر (للمونتاج)

int          g_codec          = 0;     // صيغة الضغط: 0 H.264 ، 1 HEVC ، 2 AV1

int          g_themeChoice    = 0;     // الثيم: 0 داكن ، 1 أسود ، 2 كحلي

int          g_accentChoice   = 0;     // اللون الأساسي (ACCENT_COUNT = مخصّص)

UINT32       g_accentCustom   = 0x4F8CFF;   // اللون المخصّص

const wchar_t* APP_VERSION    = L"" APP_VERSION_STR;   // من Version.h

const wchar_t* APP_REPO       = L"levi113160909-bit/Screen-Recorder";

UpdateInfo   g_update;                 // نتيجة آخر فحص للتحديثات

int          g_updateCheckDay = 0;     // آخر يوم فحصنا فيه (مرة باليوم بس)

bool         g_updateChecking = false;

int          g_overlayCorner  = 1;     // مؤشر التسجيل: 0 مخفي ، 1 أعلى اليمين ، 2 أعلى اليسار ، 3 أسفل اليمين ، 4 أسفل اليسار

RecordingOverlay g_overlay;

// أيقونة جنب الساعة: ✕ بيخبّي البرنامج بدل ما يسكّره
TrayIcon     g_tray;

bool         g_closeToTray   = true;   // ✕ = يضل شغّال جنب الساعة

bool         g_toastsOn      = true;   // الإشعارات بزاوية الشاشة
bool         g_runAsAdmin    = false;  // التشغيل كمسؤول (للألعاب اللي شغّالة كمسؤول)
bool         g_elevationDeclined = false;
bool         g_startupTask   = false;

bool         g_overlayOverApp = true;  // المؤشر والإشعارات بيبيّنوا حتى لما نافذة البرنامج قدامك

int          g_settingsPage  = 0;      // آخر قسم فتحته بنافذة الإعدادات

bool         g_trayHintShown = false;  // نبّهناه مرة إنه لسا شغّال؟

bool         g_reallyQuit    = false;  // "خروج" من القائمة (مش ✕)

std::wstring g_encoderName;            // اسم المشفّر المختار (لبطاقة الرسومات)

std::vector<Recorder::EncoderInfo> g_encoders;

std::wstring g_settingsPath;
