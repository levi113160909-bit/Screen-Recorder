// =============================================================
//  App.h - الأشياء المشتركة بين ملفات البرنامج الرئيسي
//
//  البرنامج مقسوم لملفات حسب الميزة (Recording.cpp، Streaming.cpp...)
//  وكل ملف بيبلّش بـ #include "App.h". هون بتلاقي:
//    1) المكتبات اللي الكل بيحتاجها
//    2) الأنواع والثوابت المشتركة
//    3) المتغيرات المشتركة (extern): مين صاحبها مكتوب فوق كل مجموعة
//    4) الدوال اللي بتنستعمل من أكتر من ملف، مقسومة حسب الملف اللي فيه تعريفها
//
//  الشي اللي بينستعمل بملف واحد بس بيضل جوّا ملفه (مش هون).
//  أرقام عناصر الواجهة بـ AppIds.h
// =============================================================
#pragma once

#ifndef NOMINMAX
#define NOMINMAX      // بيمنع ويندوز يخرّب std::min و std::max
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <mfapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <tuple>
#include <cmath>
#include <string>
#include <vector>
#include "Capture.h"
#include "Scene.h"
#include "Audio.h"
#include "Recorder.h"
#include "Streamer.h"
#include "Media.h"
#include "Ui.h"
#include "Panel.h"
#include "MicFiltersPanel.h"
#include "AppAudio.h"
#include "AppAudioPanel.h"
#include "AddSourcePanel.h"
#include "Overlay.h"
#include "Hotkeys.h"
#include "HotkeysPanel.h"
#include "Tray.h"
#include "ReplayPanel.h"
#include "TargetPanel.h"
#include "ColorPickerPanel.h"
#include "Screenshot.h"
#include "Updater.h"
#include "RegionSelect.h"
#include "ChromaPanel.h"
#include "GalleryPanel.h"
#include "TrimPanel.h"
#include "GameDetect.h"
#include "FirstRunPanel.h"
#include "SettingsPanel.h"
#include "Tooltip.h"
#include "PlayerPanel.h"
#include "BrowserSource.h"
#include "VirtualCam.h"
#include "CrashLog.h"
#include <shobjidl.h>
#include <commdlg.h>
#include <wincrypt.h>
#include <wincodec.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfplat.lib")


using Microsoft::WRL::ComPtr;

#include "AppIds.h"

// =============================================================
//  الأنواع المشتركة
// =============================================================

// كل المشاهد: الشغّال طبقاته بـ g_scene ، والباقي محفوظين هون
struct SceneData
{
    std::wstring            name;
    std::vector<SceneLayer> layers;
    std::vector<AppAudioEntry> apps;   // أصوات البرامج بهالمشهد
    HotkeyBinding           hotkey;    // اختصار التبديل لهالمشهد (بنافذة الاختصارات)
};

// السحب بالماوس فوق المعاينة
enum class Drag { None, Move, Resize };

// أيقونة خيار بالقائمة: الرمز ، و danger = أحمر (خروج ، إيقاف)
struct MenuGlyph
{
    const wchar_t* glyph = nullptr;
    bool           danger = false;
};

// أيقونات الإشعارات (Segoe MDL2)
namespace Glyph
{
    constexpr wchar_t Record = 0xE7C8, Check = 0xE73E, Pause = 0xE769, Play = 0xE768, Live = 0xE93E,
                      Replay = 0xE81C, Mic = 0xE720, MicOff = 0xEC54, Scene = 0xE7F4, Warning = 0xE7BA, Stop = 0xE71A,
                      Camera = 0xE722, Flag = 0xE7C1, Game = 0xE7FC;
}

// =============================================================
//  الثوابت المشتركة
// =============================================================

inline constexpr UINT_PTR TIMER_FRAME = 1;

inline const UINT32              STREAM_PRESETS[] = { 3000, 4500, 6000, 8000, 10000, 12000 };

inline constexpr UINT32          STREAM_KBPS_MAX = 50000;

inline const int                     REPLAY_SECONDS[] = { 15, 30, 60, 120 };

inline const int TRANSITION_MS[]    = { 0, 300, 700 };

// -------- الإعدادات (بتنحفظ بملف settings.ini) --------
// معدّل الإطارات: 0 تلقائي ، 1 = 30 ، 2..7 = 60 75 100 120 144 165 ، 8 = متغيّر "مثل اللعبة"
// (المتغيّر: حتى 240، ومعدّل البت على أساس 120)
inline const UINT32 FPS_OPTIONS[]    = { 0, 30, 60, 75, 100, 120, 144, 165, 120 };

inline constexpr int FPS_VARIABLE   = 8;

inline constexpr int FPS_FIXED_FIRST = 2;  // أول خيار بسطر الأرقام (60)

inline const float  QUALITY_BPP[]    = { 0.06f, 0.11f, 0.20f };   // بت لكل بكسل بكل إطار (منخفضة/متوسطة/عالية)

inline const int    CANVAS_H[]       = { 0, 1440, 1080, 720 };     // 0 = بحجم الشاشة الرئيسية

inline const UINT   WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");   // شريط المهام رجع اشتغل

inline constexpr UINT WM_SHOWAPP = WM_APP + 21;   // نسخة تانية من البرنامج انفتحت: منبيّن هالنسخة

inline const wchar_t MENU_CLASS[] = L"ScreenRecorderMenu";

inline const wchar_t INPUT_CLASS[] = L"ScreenRecorderInput";

inline constexpr int  PLATFORM_COUNT = 6;

// =============================================================
//  المتغيرات المشتركة (تعريفها بالملف المكتوب فوق كل مجموعة)
// =============================================================

// ---- main.cpp ----
extern bool g_ignoreNextUp;   // بعد الدبل كليك ما منعتبر الكبسة مرتين
extern D2D1_RECT_F g_appVolTrack;
extern int g_overlayLastCorner;   // لما تخفيه بالاختصار ومنرجّعه، بيرجع لنفس الزاوية

// ---- AppState.cpp ----
extern HotkeyManager g_hotkeys;   // الاختصارات (بتتغيّر من نافذة "اختصارات لوحة المفاتيح")
extern HWND g_hwnd;
extern UINT g_dpi;
extern ComPtr<ID2D1Factory> g_d2d;
extern float g_scroll;   // تمرير قائمة الطبقات
extern float g_scrollMax;   // أقصى تمرير للتبويب الحالي (لحتى العجلة توقف عند النهاية)
extern D2D1_RECT_F g_listRect;
extern Scene g_scene;
extern std::vector<SceneData> g_scenes;
extern int g_activeScene;
extern int g_selectedLayer;   // رقم الطبقة المختارة (0 = ولا وحدة)
extern Drag g_drag;
extern int g_dragHandle;   // أي مقبض (0..7) عم نسحب
extern std::vector<double> g_guidesX;
extern std::vector<double> g_guidesY;   // خطوط المغناطيس اللي بتبيّن وقت السحب (ببكسل اللوحة)
extern AudioSource g_systemAudio;
extern AudioSource g_micAudio;
extern bool g_systemOn;
extern bool g_micOn;
extern AudioSource* g_volDrag;   // شريط العلو اللي عم نسحبه بالماوس
extern D2D1_RECT_F g_volTrack[2];   // مكان شريط العلو (0 = النظام ، 1 = الميكروفون)
extern float g_systemLevel;
extern float g_micLevel;
extern AppAudioMixer g_appMixer;
extern std::vector<float> g_appLevels;   // مستوى صوت كل برنامج (للمقياس)
extern std::vector<D2D1_RECT_F> g_appTracks;   // مكان شريط علو كل برنامج بآخر رسمة
extern int g_appVolDrag;   // شريط البرنامج اللي عم نسحبه (-1 = ولا واحد)
extern Streamer g_streamer;
extern std::vector<StreamTarget> g_targets;   // كل الوجهات (المفعّلة والمطفية)
extern std::vector<int> g_liveTargets;   // أرقام الوجهات اللي عم نبث عليها هلق
extern std::vector<OutputStatus> g_streamStatus;   // حالة كل وجهة شغّالة
extern int g_streamRes;   // 0 = بحجم اللوحة ، 1 = 1080p ، 2 = 720p
extern UINT32 g_streamKbps;   // معدّل البت للبث (كيلوبت/ث) - منفصل عن التسجيل
extern int g_streamFpsChoice;
extern bool g_streamSaveCopy;   // نسخة من البث على الجهاز
extern bool g_streamAdaptive;   // خفّض المعدّل لحاله إذا ضعف النت // إطارات البث: 0 مثل التسجيل (حد 60) ، 1 = 30 ، 2 = 60
extern int g_verticalMode;   // البث العمودي: 0 قصّ المنتصف ، 1 الشاشة كاملة
extern Streamer g_replay;
extern std::shared_ptr<ReplayBuffer> g_replayBuffer;
extern bool g_replayWanted;   // شغّالة هلق؟ (للحفظ)
extern ReplaySettings g_replaySet;   // إعدادات الإعادة (نافذة "إعدادات الإعادة")
extern HMONITOR g_replayMonitorHandle;
extern bool g_autoGame;
extern HWND g_gameHwnd;
extern CaptureTarget g_gameTarget;
extern bool g_replayByGame;   // الإعادة اشتغلت لحالها بسبب اللعبة
extern bool g_firstRunDone;   // انعرض الإعداد السريع؟
extern int g_transitionChoice;   // 0 فوري ، 1 تلاشي سريع ، 2 تلاشي ناعم
extern int g_fadeTicks;   // وقت التلاشي: منحدّث المعاينة أسرع
extern Recorder g_recorder;
extern std::wstring g_folder;
extern std::wstring g_lastFile;
extern std::wstring g_message;
extern bool g_messageIsError;
extern Compositor g_previewCompositor;   // false = بلا ماوس بالمعاينة
extern ComPtr<ID2D1Bitmap> g_previewBitmap;
extern D2D1_RECT_F g_previewArea;   // المساحة المتاحة للمعاينة
extern bool g_previewValid;
extern HICON g_icons[3][2];   // [عادي / عم يسجّل / متوقف مؤقتاً][كبيرة / صغيرة]
extern int g_iconState;
extern int g_tick;
extern int g_replaySavedTick;   // إمتى آخر مرة حفظنا إعادة (للمؤشر)
extern int g_tab;   // 0 = المصادر ، 1 = البث ، 2 = الإعدادات
extern int g_canvasChoice;
extern int g_fpsChoice;
extern int g_quality;   // 0 منخفضة ، 1 متوسطة ، 2 عالية ، 3 مخصّصة
extern int g_encoderMode;   // 0 تلقائي ، 1 بطاقة الرسومات ، 2 المعالج
extern bool g_crashSafe;   // MP4 آمن (مقطّع)
extern bool g_separateTracks;   // مسار صوت لكل مصدر (للمونتاج)
extern int g_codec;   // صيغة الضغط: 0 H.264 ، 1 HEVC ، 2 AV1
extern int g_themeChoice;   // الثيم: 0 داكن ، 1 أسود ، 2 كحلي
extern int g_accentChoice;   // اللون الأساسي (ACCENT_COUNT = مخصّص)
extern UINT32 g_accentCustom;   // اللون المخصّص
extern const wchar_t* APP_VERSION;
extern const wchar_t* APP_REPO;
extern UpdateInfo g_update;   // نتيجة آخر فحص للتحديثات
extern int g_updateCheckDay;   // آخر يوم فحصنا فيه (مرة باليوم بس)
extern bool g_updateChecking;
extern int g_overlayCorner;   // مؤشر التسجيل: 0 مخفي ، 1 أعلى اليمين ، 2 أعلى اليسار ، 3 أسفل اليمين ، 4 أسفل اليسار
extern RecordingOverlay g_overlay;
extern TrayIcon g_tray;
extern bool g_closeToTray;   // ✕ = يضل شغّال جنب الساعة
extern bool g_toastsOn;   // الإشعارات بزاوية الشاشة
extern bool g_overlayOverApp;   // المؤشر والإشعارات بيبيّنوا حتى لما نافذة البرنامج قدامك
extern int g_settingsPage;   // آخر قسم فتحته بنافذة الإعدادات
extern bool g_trayHintShown;   // نبّهناه مرة إنه لسا شغّال؟
extern bool g_reallyQuit;   // "خروج" من القائمة (مش ✕)
extern std::wstring g_encoderName;   // اسم المشفّر المختار (لبطاقة الرسومات)
extern std::vector<Recorder::EncoderInfo> g_encoders;
extern std::wstring g_settingsPath;

// ---- PreviewEdit.cpp ----
extern D2D1_POINT_2F g_dragStart;
extern SceneLayer g_dragOrig;
extern std::vector<BYTE> g_previewPixels;
extern int g_previewW;
extern int g_previewH;
extern D2D1_RECT_F g_previewDst;   // وين انرسمت اللوحة فعلياً

// ---- AppSettings.cpp ----
extern UINT32 g_customMbps;

// ---- ReplayControl.cpp ----
extern Scene g_replayScene;   // مشهد الإعادة لما المصدر "الشاشة كاملة"
extern bool g_systemForReplay;   // شغّلنا صوت النظام بس مشان الإعادة

// ---- Recording.cpp ----
extern int g_diskWarnLevel;   // 0 تمام ، 1 نبّهنا (أقل من 10 دقايق) ، 2 وقّفنا
extern std::vector<LONGLONG> g_markers;   // العلامات: بالملي ثانية من بداية التسجيل
extern int g_pttReleaseTick;   // اضغط لتتكلم: إمتى نسكّر المايك بعد ما تترك الزر
extern bool g_pttWasOn;   // 0 تمام ، 1 نبّهنا (أقل من 10 دقايق) ، 2 وقّفنا

// ---- Commands.cpp ----
extern const wchar_t* APP_URL;

// ---- Streaming.cpp ----
extern ComPtr<ID2D1RenderTarget> g_logoRt;   // الرسّام اللي انعملت عليه الصور
extern ComPtr<ID2D1Bitmap> g_logoBmp[PLATFORM_COUNT];

// =============================================================
//  الدوال المشتركة (حسب الملف اللي فيه تعريفها)
// =============================================================

// ---- PreviewEdit.cpp: المعاينة: رسم المشهد المصغّر، وتحريك وتكبير الطبقات بالماوس مع الالتصاق ----
void UpdatePreview();
void DrawPreview(const D2D1_RECT_F& area);
int LayerAt(D2D1_POINT_2F p);
int HandleAt(D2D1_POINT_2F p);
bool BeginDrag(D2D1_POINT_2F p);
void UpdateDrag(D2D1_POINT_2F p, bool shift, bool noSnap, bool crop);
void NudgeSelected(double dx, double dy);

// ---- AppSettings.cpp: الإعدادات: الإطارات ومعدّل البت والمشفّر، وحفظ وتحميل ملف الإعدادات، والتشغيل مع ويندوز ----
UINT32 MonitorHz();
UINT32 CurrentFps();
UINT32 StreamFps();
void ChangeStreamKbps(int direction);
UINT32 CurrentBitrate();
std::wstring MbpsText(UINT32 bps);
void ChangeBitrate(int direction);
std::wstring EncoderLabel();
void ShowEncoderMenu(POINT pt);
void SaveSettings();
void LoadSettings();
bool IsRunAtStartup();
void SetRunAtStartup(bool on);

// ---- ReplayControl.cpp: الإعادة: التسجيل المستمر بالذاكرة، الحفظ، وكشف الألعاب ----
std::wstring ReplayFolder();
std::vector<CaptureTarget> ReplayMonitors();
void StartReplay(bool quiet = false);
void StopReplay();
void RestartReplayQuiet();
void StopReplayQuiet();
void CheckGame(HWND hwnd);
void SaveReplay();
void RestartReplayIfActive();

// ---- Scenes.cpp: المشاهد: إضافة، تبديل، تسمية، نسخ، حذف، وحفظ وتحميل من الملف ----
std::wstring ScenesPath();
HMONITOR RecordedMonitor();
void SyncSceneHotkeys();
void SceneHotkeysFromManager();
bool AnySceneHasApps();
void SwitchScene(int index);
void AddScene();
std::wstring Clean(std::wstring s);
void SaveScenes();
std::vector<std::wstring> SplitTabs(const std::wstring& line);
bool LoadScenes();
void FollowWindowAspect();
void ReconnectLostWindows();
void RenameScene(int index);
void ShowSceneMenu(int index, POINT pt);

// ---- Recording.cpp: التسجيل: البدء والإيقاف والإيقاف المؤقت، العلامات (فصول يوتيوب)، ومراقبة مساحة القرص ----
void AddMarker();
void FinishRecording(HRESULT hr);
ULONGLONG FreeBytes(const std::wstring& folder);
std::wstring DiskSummary(ULONGLONG freeB, UINT32 bitrate, bool& low);
void CheckDiskSpace();
void ToggleRecording();
void TogglePause();

// ---- Commands.cpp: تنفيذ الكبسات: كل زر بالواجهة الرئيسية بيوصل لهون برقمه (شوف AppIds.h) ----
void OnClick(int id, D2D1_POINT_2F p);

// ---- AppUtil.cpp: أدوات صغيرة مشتركة: رسائل وإشعارات، أسماء ملفات، تحويل إحداثيات، اختيار مجلد ----
bool Busy();
std::wstring SizeText(int w, int h);
POINT DipToScreen(float x, float y);
std::wstring FileNameOf(const std::wstring& path);
void Notify(wchar_t glyph, D2D1_COLOR_F color, const std::wstring& title, const std::wstring& sub = L"");
void ShowMessage(const std::wstring& text, bool isError);
std::wstring KeyHint(int action, const std::wstring& what);
bool PickFolder(std::wstring& path, const wchar_t* title);
void OpenFolderPath(const std::wstring& folder);
std::wstring FormatTime(LONGLONG ms);

// ---- PopupMenu.cpp: القائمة المنبثقة ونافذة الكتابة الصغيرة (بنفس ستايل البرنامج بدل قوائم ويندوز) ----
LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
int ShowMenu(const std::vector<std::wstring>& items, int checked, POINT screenPt, const std::vector<int>& icons = {}, const std::vector<MenuGlyph>& glyphs = {});
LRESULT CALLBACK InputProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool InputBox(const std::wstring& title, std::wstring& text, bool password = false, bool ltr = false, int limit = 60);

// ---- AppIcon.cpp: أيقونة البرنامج بشريط المهام (منرسمها بالكود، وبتتغيّر وقت التسجيل) ----
void CreateAppIcons();
void UpdateAppIcon();

// ---- SceneLayers.cpp: طبقات المشهد: إضافة المصادر وحذفها وقوائمها (شاشة، نافذة، كاميرا، صورة، نص، ويب...) ----
int FindLayer(int id);
void AddLayer(const CaptureTarget& target);
void RemoveLayer(int id);
void PrimaryMonitorSize(int& w, int& h);
void ApplyCanvasChoice();
void LayerCommand(int id, int cmd);
void ShowLayerMenu(int id, POINT screenPt);
void ShowAddSourceMenu(POINT);

// ---- AudioControl.cpp: الصوت: تشغيل/إطفاء المصادر، اختيار الجهاز، ومنزلقات العلو ----
void SetAudioEnabled(AudioSource& src, bool& flag, bool on);
void ShowDeviceMenu(AudioSource& src, bool on, AudioSource::Kind kind, POINT pt);
float LevelToMeter(float peak);
void SetVolumeFromX(AudioSource& src, const D2D1_RECT_F& track, float x);
float VolumeAtX(const D2D1_RECT_F& track, float x);

// ---- Streaming.cpp: البث المباشر: الوجهات (يوتيوب، تويتش...)، شعاراتها، مفاتيحها المشفّرة، والبدء والإيقاف ----
void LoadTargets();
void DrawPlatformIcon(int p, const D2D1_RECT_F& r, bool enabled);
void AddTarget(POINT pt);
UINT32 TargetKbps(const StreamTarget& t);
bool TargetHevc(const StreamTarget& t);
void UploadEstimate(UINT32& totalKbps, int& encodes);
void ShowTargetMenu(int index, POINT pt);
void ToggleTarget(int index);
void StreamSize(UINT32& w, UINT32& h);
void StopStreaming(const std::wstring& message = L"انتهى البث");
void ToggleStreaming();
const OutputStatus* TargetStatus(int index);
int LiveCount();

// ---- FirstRunSetup.cpp: الإعداد السريع بأول تشغيل (لقطات / تسجيل / بث) ----
void ShowFirstRun();

// ---- AppPanels.cpp: فتح النوافذ الداخلية: الإعدادات، المعرض، المشغّل، القص، فلاتر المايك ----
void ShowMicFilters();
void OpenGallery();
void OpenSettings(int page);

// ---- UiFrame.cpp: هيكل الواجهة: الشريط العلوي والجانبي والسفلي، والرسم الكامل لكل إطار ----
void DiscardRenderTarget();
void Paint();

// ---- UiSourcesTab.cpp: تبويب المصادر: قائمة الطبقات وصفوف الصوت وأصوات البرامج ----
void DrawSourcesTab(const D2D1_RECT_F& side, float y);

// ---- SettingsPages.cpp: صفحات نافذة الإعدادات اللي مربوطة مباشرة بإعدادات البرنامج ----
float DrawSettingsPage(int page, float L, float top, float R);

// ---- UiStreamTab.cpp: تبويب البث: الوجهات، ملخّص الإعدادات، عدّاد الرفع، والكاميرا الافتراضية ----
void ToggleVirtualCam();
void DrawStreamTab(const D2D1_RECT_F& side, float y);

// ---- AppTray.cpp: أيقونة جنب الساعة: الإخفاء والإظهار، قائمتها، والخروج ----
void ShowFromTray();
void HideToTray();
void QuitApp();
void ShowTrayMenu(POINT pt);

// =============================================================
//  فحوصات وقت البناء
// =============================================================

// رسائل البرنامج الخاصة (WM_APP + رقم): كل رقم لرسالة وحدة بس.
// لتضيف رسالة: خد أول رقم فاضي، وضيفها هون (المترجم بيتأكد ما في تكرار). الجدول بـ docs/ARCHITECTURE.md
namespace MsgCheck
{
    inline constexpr UINT ALL[] = {
        WM_RECORDER_DONE, WM_STREAMER_DONE, WM_TRAY, WM_SHOWAPP, WM_HOTKEY_CAPTURE, WM_PUSH_TO_TALK,
        WM_SCREENSHOT_DONE, WM_UPDATE_RESULT, WM_STREAM_COPY_SAVED, WM_VCAM_RESULT, WM_TEST_CRASH };
    constexpr bool Unique()
    {
        for (size_t i = 0; i < std::size(ALL); ++i)
            for (size_t j = i + 1; j < std::size(ALL); ++j)
                if (ALL[i] == ALL[j]) return false;
        return true;
    }
    static_assert(Unique(), "رسالتين WM_APP بنفس الرقم");
}
