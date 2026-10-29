// =============================================================
//  Screen Recorder - main.cpp
//  الواجهة: منرسمها بنفسنا بـ Direct2D (رسم حديث وسريع)
//  و DirectWrite (كتابة النصوص، وبيدعم العربي صح)
//
//  الفكرة: كل 33 ملي ثانية منعيد رسم النافذة كلها من الصفر.
//  ووقت الرسم منسجّل وين كل زر، لحتى لما تكبس نعرف شو كبست.
// =============================================================
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
#include "SettingsPanel.h"
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

// أرقام العناصر اللي بتنكبس
enum : int
{
    ID_RECORD = 1, ID_PAUSE, ID_FOLDER, ID_ADD_SOURCE, ID_SYSTEM_AUDIO, ID_MIC,
    ID_SYSTEM_DEVICE, ID_MIC_DEVICE, ID_ENCODER_MENU,
    ID_TAB_SOURCES = 10, ID_TAB_STREAM, ID_TAB_SETTINGS,     // التبويبات (لازم ورا بعض)
    ID_BITRATE_MINUS, ID_BITRATE_PLUS, ID_ADD_SCENE,
    ID_SYSTEM_MUTE, ID_MIC_MUTE, ID_SYSTEM_VOL, ID_MIC_VOL,
    ID_STREAM, ID_ADD_TARGET, ID_STREAM_KBPS_MINUS, ID_STREAM_KBPS_PLUS,
    ID_REPLAY, ID_REPLAY_STOP, ID_MIC_FILTERS,
    ID_CANVAS_FIRST  = 90,      // 4 خيارات
    ID_FPS_FIRST     = 100,     // 4 خيارات
    ID_QUALITY_FIRST = 110,     // 4 خيارات
    ID_CONTAINER_FIRST = 130,   // خيارين
    ID_STREAM_RES_FIRST = 140,  // 3 خيارات (دقة البث)
    ID_REPLAY_SEC_FIRST = 150,  // 4 خيارات (مدة الإعادة)
    ID_TRANSITION_FIRST = 160,  // 3 خيارات (الانتقال بين المشاهد)
    ID_VERTICAL_FIRST   = 170,  // خيارين (طريقة البث العمودي)
    ID_AUDIO_TRACKS_FIRST = 180,  // خيارين (مسار صوت واحد / مسارات منفصلة)
    ID_OVERLAY_FIRST    = 190,  // 5 خيارات (مؤشر التسجيل: مخفي + 4 زوايا)
    ID_HOTKEYS          = 200,  // زر نافذة الاختصارات
    ID_CLOSE_FIRST      = 210,  // خيارين (✕ = جنب الساعة / إغلاق)
    ID_STARTUP_FIRST    = 220,  // خيارين (التشغيل مع ويندوز)
    ID_REPLAY_SETTINGS  = 230,  // (قديم) صار قسم بنافذة الإعدادات
    ID_OPEN_SETTINGS    = 240,  // زر الترس بالشريط العلوي
    ID_TOASTS_FIRST     = 245,  // خيارين (الإشعارات): 245 ، 246
    ID_TEST_TOAST       = 247,  // زر "تجربة"
    ID_OVERAPP_FIRST    = 248,  // خيارين (فوق نافذة البرنامج كمان؟): 248 ، 249
    ID_OVERAPP_SECOND   = 249,

    ID_FOLDER_CHANGE, ID_FOLDER_OPEN,     // مجلد التسجيلات
    ID_LAYER_FIRST   = 1000,    // الطبقة رقم id = ID_LAYER_FIRST + id
    ID_EYE_FIRST     = 3000,
    ID_DELETE_FIRST  = 5000,
    ID_SCENE_FIRST   = 7000,    // المشهد رقم i = ID_SCENE_FIRST + i
    ID_TARGET_TOGGLE_FIRST = 9000,   // تفعيل وجهة البث رقم i
    ID_TARGET_MENU_FIRST   = 9500,   // قائمة وجهة البث رقم i
    ID_APP_ROW_FIRST       = 11000,  // صوت البرنامج رقم i (بقائمة المصادر)
    ID_APP_MUTE_FIRST      = 11500,
    ID_APP_DELETE_FIRST    = 12000,
    ID_APP_VOL_FIRST       = 12500,
    ID_APP_LAST            = 13000
};

constexpr UINT_PTR TIMER_FRAME = 1;
HotkeyManager g_hotkeys;     // الاختصارات (بتتغيّر من نافذة "اختصارات لوحة المفاتيح")

// =============================================================
//  متغيرات البرنامج
// =============================================================
HWND  g_hwnd = nullptr;
UINT  g_dpi  = 96;

// أدوات الرسم (الألوان والخطوط وأدوات الرسم الصغيرة صاروا بـ Ui.h)
ComPtr<ID2D1Factory>          g_d2d;
float            g_scroll = 0;         // تمرير قائمة الطبقات
float            g_scrollMax = 0;      // أقصى تمرير للتبويب الحالي (لحتى العجلة توقف عند النهاية)
D2D1_RECT_F      g_listRect{};

// المشهد اللي شغّال هلق (هو اللي بيتسجّل وبيبيّن بالمعاينة)
Scene g_scene;

// كل المشاهد: الشغّال طبقاته بـ g_scene ، والباقي محفوظين هون
struct SceneData
{
    std::wstring            name;
    std::vector<SceneLayer> layers;
    std::vector<AppAudioEntry> apps;   // أصوات البرامج بهالمشهد
    HotkeyBinding           hotkey;    // اختصار التبديل لهالمشهد (بنافذة الاختصارات)
};
std::vector<SceneData> g_scenes;
int                    g_activeScene = 0;
bool                   g_ignoreNextUp = false;   // بعد الدبل كليك ما منعتبر الكبسة مرتين
int   g_selectedLayer = 0;             // رقم الطبقة المختارة (0 = ولا وحدة)

// السحب بالماوس فوق المعاينة
enum class Drag { None, Move, Resize };
Drag          g_drag = Drag::None;
int           g_dragHandle = -1;       // أي مقبض (0..7) عم نسحب
D2D1_POINT_2F g_dragStart{};
SceneLayer    g_dragOrig;
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
D2D1_RECT_F              g_appVolTrack{};

// البث المباشر
Streamer                  g_streamer;
std::vector<StreamTarget> g_targets;            // كل الوجهات (المفعّلة والمطفية)
std::vector<int>          g_liveTargets;        // أرقام الوجهات اللي عم نبث عليها هلق
std::vector<OutputStatus> g_streamStatus;       // حالة كل وجهة شغّالة
int                       g_streamRes  = 1;     // 0 = بحجم اللوحة ، 1 = 1080p ، 2 = 720p
UINT32                    g_streamKbps = 6000;  // معدّل البت للبث (كيلوبت/ث)
int                       g_verticalMode = 0;   // البث العمودي: 0 قصّ المنتصف ، 1 الشاشة كاملة

// الإعادة (آخر ثواني بالذاكرة)
Streamer                      g_replay;
std::shared_ptr<ReplayBuffer> g_replayBuffer = std::make_shared<ReplayBuffer>();
bool                          g_replayWanted = false;      // شغّالة هلق؟ (للحفظ)
ReplaySettings                g_replaySet;                 // إعدادات الإعادة (نافذة "إعدادات الإعادة")
Scene                         g_replayScene;               // مشهد الإعادة لما المصدر "الشاشة كاملة"
HMONITOR                      g_replayMonitorHandle = nullptr;
bool                          g_systemForReplay = false;   // شغّلنا صوت النظام بس مشان الإعادة
const int                     REPLAY_SECONDS[] = { 15, 30, 60, 120 };

// الانتقال بين المشاهد
int       g_transitionChoice = 1;                          // 0 فوري ، 1 تلاشي سريع ، 2 تلاشي ناعم
const int TRANSITION_MS[]    = { 0, 300, 700 };
int       g_fadeTicks = 0;                                 // وقت التلاشي: منحدّث المعاينة أسرع


// التسجيل
Recorder     g_recorder;
std::wstring g_folder, g_lastFile, g_message;
bool         g_messageIsError = false;

// المعاينة
Compositor            g_previewCompositor(false);   // false = بلا ماوس بالمعاينة
std::vector<BYTE>     g_previewPixels;
int                   g_previewW = 0, g_previewH = 0;
ComPtr<ID2D1Bitmap>   g_previewBitmap;
D2D1_RECT_F           g_previewArea{};     // المساحة المتاحة للمعاينة
D2D1_RECT_F           g_previewDst{};      // وين انرسمت اللوحة فعلياً
bool                  g_previewValid = false;

// أيقونات البرنامج (شريط المهام)
HICON g_icons[3][2] = {};              // [عادي / عم يسجّل / متوقف مؤقتاً][كبيرة / صغيرة]
int   g_iconState = -1;

int g_tick = 0;
int g_replaySavedTick = -1000;       // إمتى آخر مرة حفظنا إعادة (للمؤشر)
int g_tab  = 0;      // 0 = المصادر ، 1 = البث ، 2 = الإعدادات

// -------- الإعدادات (بتنحفظ بملف settings.ini) --------
const UINT32 FPS_OPTIONS[]    = { 0, 24, 30, 60 };          // 0 = تلقائي
const float  QUALITY_BPP[]    = { 0.04f, 0.07f, 0.12f };   // بت لكل بكسل بكل إطار (منخفضة/متوسطة/عالية)
const int    CANVAS_H[]       = { 0, 1440, 1080, 720 };     // 0 = بحجم الشاشة الرئيسية
int          g_canvasChoice   = 0;
int          g_fpsChoice      = 0;
int          g_quality        = 2;     // 0 منخفضة ، 1 متوسطة ، 2 عالية ، 3 مخصّصة
UINT32       g_customMbps     = 12;
int          g_encoderMode    = 0;     // 0 تلقائي ، 1 بطاقة الرسومات ، 2 المعالج
bool         g_crashSafe      = true;  // MP4 آمن (مقطّع)
bool         g_separateTracks = false; // مسار صوت لكل مصدر (للمونتاج)
int          g_overlayCorner  = 1;     // مؤشر التسجيل: 0 مخفي ، 1 أعلى اليمين ، 2 أعلى اليسار ، 3 أسفل اليمين ، 4 أسفل اليسار
RecordingOverlay g_overlay;
int          g_overlayLastCorner = 1;  // لما تخفيه بالاختصار ومنرجّعه، بيرجع لنفس الزاوية

// أيقونة جنب الساعة: ✕ بيخبّي البرنامج بدل ما يسكّره
TrayIcon     g_tray;
bool         g_closeToTray   = true;   // ✕ = يضل شغّال جنب الساعة
bool         g_toastsOn      = true;   // الإشعارات بزاوية الشاشة
bool         g_overlayOverApp = true;  // المؤشر والإشعارات بيبيّنوا حتى لما نافذة البرنامج قدامك
int          g_settingsPage  = 0;      // آخر قسم فتحته بنافذة الإعدادات
bool         g_trayHintShown = false;  // نبّهناه مرة إنه لسا شغّال؟
bool         g_reallyQuit    = false;  // "خروج" من القائمة (مش ✕)
const UINT   WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");   // شريط المهام رجع اشتغل
constexpr UINT WM_SHOWAPP = WM_APP + 21;   // نسخة تانية من البرنامج انفتحت: منبيّن هالنسخة
std::wstring g_encoderName;            // اسم المشفّر المختار (لبطاقة الرسومات)
std::vector<Recorder::EncoderInfo> g_encoders;
std::wstring g_settingsPath;

void SaveSettings();   // معرّفة تحت
void SaveScenes();
void RestartReplayIfActive();

// في شي شغّال (تسجيل أو بث)؟ وقتها منقفل الإعدادات اللي بتأثّر عليهم
bool Busy() { return g_recorder.IsRecording() || g_streamer.IsActive(); }

// "1920×1080" مع علامات لحتى ما تنقلب الأرقام بنص عربي
std::wstring SizeText(int w, int h)
{
    return L"\u202A" + std::to_wstring(w) + L"×" + std::to_wstring(h) + L"\u202C";
}

// =============================================================
//  قائمة منبثقة بنفس ستايل البرنامج (بدل قوائم ويندوز الرمادية)
//  بترجّع رقم الخيار أو -1 إذا انلغت. النص الفاضي = خط فاصل
// =============================================================
const wchar_t MENU_CLASS[] = L"ScreenRecorderMenu";
constexpr float MENU_ITEM_H = 34, MENU_SEP_H = 9, MENU_PAD = 6;

// أيقونة المنصة (معرّفة تحت) - منستعملها بالقوائم كمان
void DrawPlatformIcon(int platform, const D2D1_RECT_F& r, bool enabled);

struct MenuState
{
    std::vector<std::wstring> items;
    std::vector<int>          icons;     // أيقونة منصة لكل خيار (-1 = بلا)
    int   checked = -1;
    int   hover   = -1;
    int   result  = -1;
    bool  done    = false;
    UINT  dpi     = 96;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush>  brush;
};

// وين كل خيار (بنقاط DIP)
float MenuItemTop(const MenuState& m, int index)
{
    float y = MENU_PAD;
    for (int i = 0; i < index; ++i)
        y += m.items[i].empty() ? MENU_SEP_H : MENU_ITEM_H;
    return y;
}

int MenuItemAt(const MenuState& m, float y)
{
    float top = MENU_PAD;
    for (int i = 0; i < (int)m.items.size(); ++i)
    {
        const float h = m.items[i].empty() ? MENU_SEP_H : MENU_ITEM_H;
        if (y >= top && y < top + h)
            return m.items[i].empty() ? -1 : i;
        top += h;
    }
    return -1;
}

void PaintMenu(HWND hwnd, MenuState& m)
{
    if (!m.rt)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        props.dpiX = props.dpiY = (float)m.dpi;
        g_d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)), &m.rt);
        if (!m.rt)
            return;
        m.rt->CreateSolidColorBrush(Theme::Text, &m.brush);
    }

    // منستعمل نفس أدوات الرسم تبع النافذة الرئيسية (منبدّلهم مؤقتاً)
    ComPtr<ID2D1HwndRenderTarget> mainRt = g_rt;
    ComPtr<ID2D1SolidColorBrush>  mainBrush = g_brush;
    g_rt    = m.rt;
    g_brush = m.brush;

    g_rt->BeginDraw();
    const D2D1_SIZE_F size = g_rt->GetSize();
    g_rt->Clear(Theme::Panel);
    Stroke(Rect(0.5f, 0.5f, size.width - 0.5f, size.height - 0.5f), Theme::Border, 8, 1);

    for (int i = 0; i < (int)m.items.size(); ++i)
    {
        const float top = MenuItemTop(m, i);
        if (m.items[i].empty())     // خط فاصل
        {
            Fill(Rect(12, top + 4, size.width - 12, top + 5), Theme::Border);
            continue;
        }
        const D2D1_RECT_F row = Rect(MENU_PAD, top, size.width - MENU_PAD, top + MENU_ITEM_H);
        if (i == m.hover)
            Fill(row, Theme::Hover, 6);
        if (i == m.checked)
        {
            Fill(Rect(row.right - 3, row.top + 8, row.right, row.bottom - 8), Theme::Accent, 1.5f);   // خط أزرق
            Text(L"\uE73E", g_fIcon.Get(), Rect(row.right - 32, row.top, row.right - 6, row.bottom), Theme::Accent);
        }
        float textRight = row.right - 36;
        if (i < (int)m.icons.size() && m.icons[i] >= 0)
        {
            const float cy = (row.top + row.bottom) / 2;
            DrawPlatformIcon(m.icons[i], Rect(row.right - 34, cy - 12, row.right - 10, cy + 12), true);
            textRight = row.right - 44;
        }
        Text(m.items[i], g_fBody.Get(), Rect(row.left + 12, row.top, textRight, row.bottom),
             i == m.checked ? Theme::Accent : Theme::Text);
    }

    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET)
    {
        m.brush.Reset();
        m.rt.Reset();
    }
    g_rt    = mainRt;
    g_brush = mainBrush;
}

LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* m = (MenuState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!m)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    const float s = 96.0f / m->dpi;
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        PaintMenu(hwnd, *m);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
    {
        const int hover = MenuItemAt(*m, GET_Y_LPARAM(lParam) * s);
        if (hover != m->hover)
        {
            m->hover = hover;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        m->hover = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP:
    {
        const int i = MenuItemAt(*m, GET_Y_LPARAM(lParam) * s);
        if (i >= 0)
        {
            m->result = i;
            m->done   = true;
        }
        return 0;
    }

    case WM_KEYDOWN:      // الكيبورد: أسهم + Enter + Esc
    {
        const int n = (int)m->items.size();
        if (wParam == VK_ESCAPE) { m->done = true; return 0; }
        if (wParam == VK_RETURN && m->hover >= 0) { m->result = m->hover; m->done = true; return 0; }
        if (wParam == VK_DOWN || wParam == VK_UP)
        {
            const int dir = (wParam == VK_DOWN) ? 1 : -1;
            int i = m->hover;
            for (int k = 0; k < n; ++k)
            {
                i = (i + dir + n) % n;
                if (!m->items[i].empty()) break;
            }
            m->hover = i;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_ACTIVATE:     // كبست برّا القائمة: منسكّرها
        if (LOWORD(wParam) == WA_INACTIVE)
            m->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ShowMenu(const std::vector<std::wstring>& items, int checked, POINT screenPt, const std::vector<int>& icons = {})
{
    MenuState m;
    m.items   = items;
    m.icons   = icons;
    m.checked = checked;

    // دقة الشاشة اللي عليها النقطة
    HMONITOR monitor = MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, MENU_CLASS, L"", WS_POPUP,
                                screenPt.x, screenPt.y, 10, 10, g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
        return -1;
    m.dpi = GetDpiForWindow(hwnd);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&m);

    // العرض = أطول نص + مسافات
    float textW = 0;
    for (const std::wstring& item : items)
    {
        ComPtr<IDWriteTextLayout> layout;
        if (!item.empty() && SUCCEEDED(g_dwrite->CreateTextLayout(item.c_str(), (UINT32)item.size(), g_fBody.Get(), 2000, 50, &layout)))
        {
            DWRITE_TEXT_METRICS tm{};
            layout->GetMetrics(&tm);
            textW = std::max(textW, tm.widthIncludingTrailingWhitespace);
        }
    }
    const float wDip = std::clamp(textW + 60 + MENU_PAD * 2, 200.0f, 560.0f);
    const float hDip = MenuItemTop(m, (int)items.size()) + MENU_PAD;
    const int   w = (int)(wDip * m.dpi / 96.0f);
    const int   h = (int)(hDip * m.dpi / 96.0f);

    // القائمة بتطلع لليسار من النقطة (لأن الواجهة عربية)، ومنضل جوّا الشاشة
    int x = screenPt.x - w, y = screenPt.y;
    if (x < mi.rcWork.left)       x = mi.rcWork.left;
    if (x + w > mi.rcWork.right)  x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom) y = std::max((int)mi.rcWork.top, (int)screenPt.y - h);

    // زوايا مدوّرة بويندوز 11
    const int corner = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    // حلقة رسائل خاصة لحتى يختار المستخدم
    MSG msg;
    while (!m.done && GetMessageW(&msg, nullptr, 0, 0))
    {
        // كبسة على أي نافذة تانية = إلغاء
        if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
            && msg.hwnd != hwnd)
        {
            m.done = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
        {
            PostQuitMessage((int)msg.wParam);
            break;
        }
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    m.brush.Reset();
    m.rt.Reset();
    DestroyWindow(hwnd);
    return m.result;
}

// =============================================================
//  نافذة كتابة صغيرة (لتسمية المشهد) بنفس ستايل البرنامج
// =============================================================
const wchar_t INPUT_CLASS[] = L"ScreenRecorderInput";

struct InputState
{
    std::wstring title;
    HWND         edit = nullptr;
    HFONT        font = nullptr;
    HBRUSH       editBrush = nullptr;
    bool         done = false, ok = false;
    UINT         dpi = 96;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush>  brush;
};

LRESULT CALLBACK InputProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* st = (InputState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (!st->rt)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
            props.dpiX = props.dpiY = (float)st->dpi;
            g_d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)), &st->rt);
            if (st->rt) st->rt->CreateSolidColorBrush(Theme::Text, &st->brush);
        }
        if (st->rt)
        {
            // منستعمل أدوات الرسم تبع البرنامج (منبدّلها مؤقتاً)
            ComPtr<ID2D1HwndRenderTarget> mainRt = g_rt;
            ComPtr<ID2D1SolidColorBrush>  mainBrush = g_brush;
            g_rt = st->rt;
            g_brush = st->brush;
            g_rt->BeginDraw();
            const D2D1_SIZE_F s = g_rt->GetSize();
            g_rt->Clear(Theme::Panel);
            Stroke(Rect(0.5f, 0.5f, s.width - 0.5f, s.height - 0.5f), Theme::Border, 10, 1);
            Text(st->title, g_fSection.Get(), Rect(16, 10, s.width - 16, 36), Theme::Text);
            Fill(Rect(16, 42, s.width - 16, 78), Theme::Card, 8);
            Stroke(Rect(16, 42, s.width - 16, 78), Theme::Accent, 8, 1);
            Text(L"Enter للحفظ  •  Esc للإلغاء", g_fSmall.Get(), Rect(16, 84, s.width - 16, 104), Theme::SubText);
            if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) { st->brush.Reset(); st->rt.Reset(); }
            g_rt = mainRt;
            g_brush = mainBrush;
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:      // مربع الكتابة غامق متل البرنامج
    {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, RGB(0xE8, 0xEA, 0xED));
        SetBkColor(dc, RGB(0x1D, 0x21, 0x29));
        return (LRESULT)st->editBrush;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
            st->done = true;
        else if (st->edit)
            SetFocus(st->edit);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// بترجّع true إذا المستخدم ضغط Enter
// password = نخفي الحروف (مفتاح البث) ، ltr = نص إنكليزي (روابط ومفاتيح)
bool InputBox(const std::wstring& title, std::wstring& text, bool password = false, bool ltr = false, int limit = 60)
{
    InputState st;
    st.title = title;

    RECT owner;
    GetWindowRect(g_hwnd, &owner);
    st.dpi = GetDpiForWindow(g_hwnd);
    auto S = [&](float v) { return (int)(v * st.dpi / 96.0f); };
    const int w = S(ltr ? 460 : 380), h = S(112);
    const int x = (owner.left + owner.right - w) / 2;
    const int y = (owner.top + owner.bottom - h) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, INPUT_CLASS, L"", WS_POPUP,
                                x, y, w, h, g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
        return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);
    const int corner = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    st.font      = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    st.editBrush = CreateSolidBrush(RGB(0x1D, 0x21, 0x29));
    DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | (ltr ? ES_LEFT : ES_RIGHT);
    if (password) style |= ES_PASSWORD;
    st.edit = CreateWindowExW(ltr ? 0 : WS_EX_RTLREADING, L"EDIT", text.c_str(), style,
                              S(26), S(50), w - S(52), S(22), hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(st.edit, WM_SETFONT, (WPARAM)st.font, TRUE);
    if (password)
        SendMessageW(st.edit, EM_SETPASSWORDCHAR, (WPARAM)L'•', 0);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);              // النص كلّه محدّد
    SendMessageW(st.edit, EM_LIMITTEXT, limit, 0);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(st.edit);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE))
        {
            st.ok   = (msg.wParam == VK_RETURN);
            st.done = true;
            break;
        }
        if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
            && msg.hwnd != hwnd && msg.hwnd != st.edit)
        {
            st.done = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
        {
            PostQuitMessage((int)msg.wParam);
            break;
        }
    }

    if (st.ok)
    {
        std::vector<wchar_t> buf(limit + 2, L'\0');
        GetWindowTextW(st.edit, buf.data(), limit + 1);
        text = buf.data();
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    st.brush.Reset();
    st.rt.Reset();
    DestroyWindow(hwnd);
    DeleteObject(st.font);
    DeleteObject(st.editBrush);
    SetForegroundWindow(g_hwnd);
    return st.ok;
}

POINT DipToScreen(float x, float y)
{
    POINT pt = { (LONG)(x * g_dpi / 96.0f), (LONG)(y * g_dpi / 96.0f) };
    ClientToScreen(g_hwnd, &pt);
    return pt;
}

// =============================================================
//  أيقونة البرنامج (بشريط المهام) - منرسمها بالكود
//  state: 0 = جاهز ، 1 = عم يسجّل ، 2 = متوقف مؤقتاً
// =============================================================
HICON MakeAppIcon(int size, int state)
{
    const UINT32 bgColor[] = { 0x16191F, 0xE5484D, 0xF5A524 };
    std::vector<UINT32> px((size_t)size * size);
    const int SS = 4;   // 4×4 عينات لكل بكسل = حواف ناعمة

    for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
    {
        double r = 0, g = 0, b = 0;
        int inside = 0;
        for (int sy = 0; sy < SS; ++sy)
        for (int sx = 0; sx < SS; ++sx)
        {
            const double fx = (x + (sx + 0.5) / SS) / size - 0.5;   // من -0.5 لـ 0.5
            const double fy = (y + (sy + 0.5) / SS) / size - 0.5;

            // مربع بزوايا مدوّرة
            const double rad = 0.22, half = 0.5 - 0.02;
            const double qx = std::fabs(fx) - (half - rad), qy = std::fabs(fy) - (half - rad);
            const double dist = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) - rad;
            if (dist > 0)
                continue;

            UINT32 c = bgColor[state];
            const double d = std::hypot(fx, fy);
            if (state == 0)                       // جاهز: دائرة حمرا ونقطة بيضا
            {
                if (d < 0.30) c = 0xE5484D;
                if (d < 0.11) c = 0xFFFFFF;
            }
            else if (state == 1)                  // عم يسجّل: دائرة بيضا كبيرة
            {
                if (d < 0.27) c = 0xFFFFFF;
            }
            else                                  // متوقف مؤقتاً: خطّين أبيض
            {
                if (std::fabs(fy) < 0.22 && (std::fabs(fx - 0.11) < 0.06 || std::fabs(fx + 0.11) < 0.06))
                    c = 0xFFFFFF;
            }
            r += (c >> 16) & 0xFF;
            g += (c >> 8) & 0xFF;
            b += c & 0xFF;
            ++inside;
        }
        if (inside == 0)
            continue;
        const UINT32 a = inside * 255 / (SS * SS);
        px[(size_t)y * size + x] = (a << 24) | ((UINT32)(r / inside) << 16) | ((UINT32)(g / inside) << 8) | (UINT32)(b / inside);
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = size;
    bmi.bmiHeader.biHeight      = -size;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color)
        return nullptr;
    memcpy(bits, px.data(), px.size() * 4);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);

    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

void CreateAppIcons()
{
    const int big   = GetSystemMetricsForDpi(SM_CXICON, g_dpi);
    const int smallSize = GetSystemMetricsForDpi(SM_CXSMICON, g_dpi);   // (small كلمة محجوزة بويندوز)
    for (int s = 0; s < 3; ++s)
    {
        g_icons[s][0] = MakeAppIcon(big, s);
        g_icons[s][1] = MakeAppIcon(smallSize, s);
    }
}

void UpdateAppIcon()
{
    // 0 جاهز ، 1 عم يسجّل ، 2 متوقف مؤقتاً ، 3 بث مباشر بس (بدون تسجيل)
    int state = !g_recorder.IsRecording() ? 0 : (g_recorder.IsPaused() ? 2 : 1);
    if (state == 0 && g_streamer.IsActive())
        state = 3;
    if (state == g_iconState)
        return;
    g_iconState = state;
    const int icon = state == 3 ? 1 : state;
    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG,   (LPARAM)g_icons[icon][0]);
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icons[icon][1]);
    const wchar_t* titles[] = { L"مسجّل الشاشة", L"● جارٍ التسجيل - مسجّل الشاشة",
                                L"❚❚ متوقف مؤقتاً - مسجّل الشاشة", L"● بث مباشر - مسجّل الشاشة" };
    SetWindowTextW(g_hwnd, titles[state]);
    g_tray.SetIcon(g_icons[icon][1]);      // أيقونة جنب الساعة كمان
    g_tray.SetTip(titles[state]);
}

// =============================================================
//  المشهد: الطبقات
// =============================================================
int FindLayer(int id)     // لازم يكون القفل ممسوك
{
    for (int i = 0; i < (int)g_scene.layers.size(); ++i)
        if (g_scene.layers[i].id == id)
            return i;
    return -1;
}

// مكان الطبقة: "ملاءمة" = أكبر حجم بيوسع بدون ما يتمطّ، بالنص
// الحجم الطبيعي لأي طبقة (الشاشة/النافذة ، الكاميرا ، الصورة ، النص)
bool LayerNaturalSize(const SceneLayer& l, int& w, int& h)
{
    const bool ok = l.kind == LayerKind::Capture ? GetCaptureSize(l.target, w, h) : MediaNaturalSize(l, w, h);
    if (ok && l.kind == LayerKind::Camera && l.shape == SHAPE_CIRCLE)
        w = h = std::min(w, h);              // الدائرة مربّعة
    return ok && w > 0 && h > 0;
}

void FitLayer(SceneLayer& l, int cw, int ch)
{
    int sw = 0, sh = 0;
    if (!LayerNaturalSize(l, sw, sh))
    {
        sw = cw;
        sh = ch;
    }
    sw = std::max(1, (int)std::lround(sw * (1 - l.cropL - l.cropR)));   // الجزء الباقي بعد القص
    sh = std::max(1, (int)std::lround(sh * (1 - l.cropT - l.cropB)));
    const double s = std::min((double)cw / sw, (double)ch / sh);
    l.w = sw * s;
    l.h = sh * s;
    l.x = (cw - l.w) / 2;
    l.y = (ch - l.h) / 2;
}

void AddLayer(const CaptureTarget& target)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    SceneLayer l;
    l.id     = g_scene.nextId++;
    l.target = target;
    FitLayer(l, g_scene.canvasW, g_scene.canvasH);
    g_scene.layers.push_back(l);          // الجديدة فوق الكل
    g_selectedLayer = l.id;
}

// كاميرا / صورة / نص: منحطّها بمكان منطقي حسب نوعها
void AddMediaLayer(SceneLayer l)
{
    int nw = 0, nh = 0;
    if (!LayerNaturalSize(l, nw, nh))       // برّا القفل (الكاميرا بتاخد لحظة لتفتح)
    {
        nw = 1280;
        nh = 720;
    }
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const double cw = g_scene.canvasW, ch = g_scene.canvasH;
    l.id = g_scene.nextId++;
    if (l.kind == LayerKind::Camera)          // بالزاوية تحت يمين
    {
        l.w = cw * 0.26;
        l.h = l.w * nh / nw;
        l.x = cw - l.w - cw * 0.025;
        l.y = ch - l.h - ch * 0.04;
    }
    else if (l.kind == LayerKind::Image)      // بالنص، مش أكبر من 60% من اللوحة
    {
        const double k = std::min(1.0, std::min(cw * 0.6 / nw, ch * 0.6 / nh));
        l.w = nw * k;
        l.h = nh * k;
        l.x = (cw - l.w) / 2;
        l.y = (ch - l.h) / 2;
    }
    else                                       // النص: تحت بالنص
    {
        double k = ch * 0.10 / nh;
        k = std::min(k, cw * 0.9 / nw);
        l.w = nw * k;
        l.h = nh * k;
        l.x = (cw - l.w) / 2;
        l.y = ch * 0.84 - l.h / 2;
    }
    g_scene.layers.push_back(l);
    g_selectedLayer = l.id;
}

std::wstring FileNameOf(const std::wstring& path);   // تحت

bool PickImageFile(std::wstring& path)
{
    wchar_t buf[MAX_PATH * 2] = {};
    if (!path.empty())
        wcsncpy_s(buf, path.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"الصور\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.tif;*.tiff\0كل الملفات\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH * 2;
    ofn.lpstrTitle  = L"اختر صورة";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return false;
    path = buf;
    return true;
}

void RemoveLayer(int id)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(id);
    if (i >= 0)
        g_scene.layers.erase(g_scene.layers.begin() + i);
    if (g_selectedLayer == id)
        g_selectedLayer = 0;
}

void PrimaryMonitorSize(int& w, int& h)
{
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi))
    {
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        return;
    }
    w = 1920;
    h = 1080;
}

// تغيير حجم اللوحة: منكبّر/منصغّر الطبقات معها
void ApplyCanvasChoice()
{
    int w = 0, h = 0;
    PrimaryMonitorSize(w, h);
    if (CANVAS_H[g_canvasChoice] > 0)     // 1440p / 1080p / 720p = 16:9
    {
        h = CANVAS_H[g_canvasChoice];
        w = h * 16 / 9;
    }
    w &= ~1;
    h &= ~1;

    std::lock_guard<std::mutex> lock(g_scene.mutex);
    if (w == g_scene.canvasW && h == g_scene.canvasH)
        return;
    const double sx = (double)w / g_scene.canvasW, sy = (double)h / g_scene.canvasH;
    for (SceneLayer& l : g_scene.layers)
    {
        l.x *= sx; l.w *= sx;
        l.y *= sy; l.h *= sy;
    }
    g_scene.canvasW = w;
    g_scene.canvasH = h;
    g_previewValid = false;
}

// عمليات القائمة (كبسة يمين على طبقة)
void LayerCommand(int id, int cmd)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(id);
    if (i < 0)
        return;
    SceneLayer& l = g_scene.layers[i];
    const int cw = g_scene.canvasW, ch = g_scene.canvasH;
    switch (cmd)
    {
    case 0: FitLayer(l, cw, ch); break;                                        // ملاءمة
    case 1: l.x = 0; l.y = 0; l.w = cw; l.h = ch; break;                        // تمديد
    case 2: { int sw, sh; if (LayerNaturalSize(l, sw, sh)) { l.w = sw * (1 - l.cropL - l.cropR); l.h = sh * (1 - l.cropT - l.cropB); }
              l.x = (cw - l.w) / 2; l.y = (ch - l.h) / 2; break; }               // الحجم الأصلي
    case 3: l.x = (cw - l.w) / 2; l.y = (ch - l.h) / 2; break;                  // توسيط
    case 5: if (i + 1 < (int)g_scene.layers.size()) std::swap(g_scene.layers[i], g_scene.layers[i + 1]); break;  // للأمام
    case 6: if (i > 0) std::swap(g_scene.layers[i], g_scene.layers[i - 1]); break;                               // للخلف
    case 8: l.visible = !l.visible; break;
    }
}

// بعد تغيير النص/الخلفية/الصورة: منظبط العرض حسب الشكل الجديد (منحافظ على الطول والنص)
void RefitAspect(int id)
{
    SceneLayer copy;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return;
        copy = g_scene.layers[i];
    }
    int nw, nh;
    if (!LayerNaturalSize(copy, nw, nh))
        return;
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(id);
    if (i < 0) return;
    SceneLayer& l = g_scene.layers[i];
    const double cx = l.x + l.w / 2;
    l.w = l.h * (nw * (1 - l.cropL - l.cropR)) / (nh * (1 - l.cropT - l.cropB));
    l.x = cx - l.w / 2;
}

const unsigned TEXT_COLORS[] = { 0xFFFFFFFF, 0xFF0E1014, 0xFFE5484D, 0xFFF5C518, 0xFF3DD68C, 0xFF4F8CFF, 0xFFFF4FA3 };
const wchar_t* TEXT_COLOR_NAMES[] = { L"أبيض", L"أسود", L"أحمر", L"أصفر", L"أخضر", L"أزرق", L"وردي" };

// أوامر خاصة بالكاميرا والصورة والنص
void LayerMediaCommand(int id, int code, POINT pt)
{
    SceneLayer copy;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return;
        copy = g_scene.layers[i];
    }
    auto modify = [&](auto&& fn)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i >= 0) fn(g_scene.layers[i]);
    };

    switch (code)
    {
    case 100:   // الشكل
    {
        const int pick = ShowMenu({ L"مستطيل", L"زوايا مدوّرة", L"دائرة" }, copy.shape, pt);
        if (pick < 0) return;
        modify([&](SceneLayer& l)
        {
            l.shape = pick;
            if (pick == SHAPE_CIRCLE && l.kind == LayerKind::Camera)   // الدائرة لازم تكون مربّعة
            {
                const double s = std::min(l.w, l.h), cx = l.x + l.w / 2, cy = l.y + l.h / 2;
                l.w = l.h = s;
                l.x = cx - s / 2;
                l.y = cy - s / 2;
            }
        });
        break;
    }
    case 101:   // مراية
        modify([](SceneLayer& l) { l.mirror = !l.mirror; });
        break;
    case 102:   // تغيير الصورة
    {
        std::wstring path = copy.source;
        if (!PickImageFile(path)) return;
        modify([&](SceneLayer& l) { l.source = path; l.target.name = FileNameOf(path); });
        RefitAspect(id);
        break;
    }
    case 103:   // تعديل النص
    {
        std::wstring text = copy.source;
        if (!InputBox(L"النص", text, false, false, 200) || text.empty()) return;
        modify([&](SceneLayer& l) { l.source = text; l.target.name = text; });
        RefitAspect(id);
        break;
    }
    case 104:   // اللون
    {
        std::vector<std::wstring> names(std::begin(TEXT_COLOR_NAMES), std::end(TEXT_COLOR_NAMES));
        int checked = -1;
        for (int k = 0; k < (int)names.size(); ++k)
            if (TEXT_COLORS[k] == copy.color) checked = k;
        const int pick = ShowMenu(names, checked, pt);
        if (pick < 0) return;
        modify([&](SceneLayer& l) { l.color = TEXT_COLORS[pick]; });
        break;
    }
    case 105:   // الخلفية
        modify([](SceneLayer& l) { l.background = !l.background; });
        RefitAspect(id);
        break;
    }
    SaveScenes();
}

// إلغاء القص: المحتوى بيضل بنفس الحجم، والطبقة بترجع تكبر لحجم المصدر كامل
void ResetCrop(int id)
{
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return;
        SceneLayer& l = g_scene.layers[i];
        const double fw = l.w / std::max(0.05, 1 - l.cropL - l.cropR);
        const double fh = l.h / std::max(0.05, 1 - l.cropT - l.cropB);
        l.x -= l.cropL * fw;
        l.y -= l.cropT * fh;
        l.w = fw;
        l.h = fh;
        l.cropL = l.cropT = l.cropR = l.cropB = 0;
    }
    SaveScenes();
}

void ShowLayerMenu(int id, POINT screenPt)
{
    bool visible = true;
    SceneLayer copy;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return;
        visible = g_scene.layers[i].visible;
        copy = g_scene.layers[i];
    }
    std::vector<std::wstring> items = { L"ملاءمة داخل اللوحة", L"تمديد لملء اللوحة", L"الحجم الأصلي", L"توسيط",
                                        L"", L"نقل إلى الأمام", L"نقل إلى الخلف",
                                        L"", visible ? L"إخفاء" : L"إظهار", L"حذف" };
    std::vector<int> codes(items.size(), -1);
    auto extra = [&](const std::wstring& label, int code) { items.push_back(label); codes.push_back(code); };
    if (copy.kind == LayerKind::Camera)
    {
        extra(L"", -1);
        extra(L"الشكل…", 100);
        extra(copy.mirror ? L"إلغاء عكس الصورة" : L"عكس الصورة (مثل المرآة)", 101);
    }
    else if (copy.kind == LayerKind::Image)
    {
        extra(L"", -1);
        extra(L"الشكل…", 100);
        extra(L"تغيير الصورة…", 102);
    }
    else if (copy.kind == LayerKind::Text)
    {
        extra(L"", -1);
        extra(L"تعديل النص…", 103);
        extra(L"لون النص…", 104);
        extra(copy.background ? L"إزالة الخلفية" : L"إضافة خلفية خلف النص", 105);
    }

    if (copy.HasCrop())
    {
        extra(L"", -1);
        extra(L"إلغاء القص", 200);
    }

    const int cmd = ShowMenu(items, -1, screenPt);
    if (cmd < 0) return;
    if (cmd >= 10 && codes[cmd] == 200) ResetCrop(id);
    else if (cmd >= 10) { if (codes[cmd] >= 0) LayerMediaCommand(id, codes[cmd], screenPt); }
    else if (cmd == 9) RemoveLayer(id);
    else               LayerCommand(id, cmd);
}

void ShowMessage(const std::wstring& text, bool isError);   // معرّفة تحت
void Notify(wchar_t glyph, D2D1_COLOR_F color, const std::wstring& title, const std::wstring& sub = L"");

// أيقونات الإشعارات (Segoe MDL2)
namespace Glyph
{
    constexpr wchar_t Record = 0xE7C8, Check = 0xE73E, Pause = 0xE769, Play = 0xE768, Live = 0xE93E,
                      Replay = 0xE81C, Mic = 0xE720, MicOff = 0xEC54, Scene = 0xE7F4, Warning = 0xE7BA, Stop = 0xE71A;
}

// نافذة "إضافة صوت برنامج" (شوف AppAudioPanel.cpp)
void ShowAddAppAudioMenu(POINT)
{
    g_panels.Open(std::make_unique<AppAudioPanel>(
        [](const std::wstring& exe)
        {
            for (const AppAudioEntry& e : g_appMixer.Entries())
                if (_wcsicmp(e.exePath.c_str(), exe.c_str()) == 0)
                    return true;
            return false;
        },
        [](const AudioApp& app)
        {
            const size_t before = g_appMixer.Count();
            g_appMixer.Add(app);
            if (g_appMixer.Count() > before && !g_appMixer.IsConnected((int)before))
                ShowMessage(L"تعذّر التقاط صوت البرنامج (يتطلّب ويندوز 10 الإصدار 2004 أو أحدث)", true);
            else
                ShowMessage(L"أُضيف صوت " + app.name, false);
            SaveScenes();
        }));
}

// نافذة "إضافة مصدر" (شوف AddSourcePanel.cpp)
void ShowAddSourceMenu(POINT)
{
    AddSourceActions a;
    a.capture = [](const CaptureTarget& t)
    {
        AddLayer(t);
        SaveScenes();
    };
    a.camera = [](const CameraInfo& c)
    {
        SceneLayer l;
        l.kind        = LayerKind::Camera;
        l.source      = c.link;
        l.target.name = c.name;
        l.shape       = SHAPE_ROUNDED;
        AddMediaLayer(l);
        SaveScenes();
    };
    a.image = []
    {
        std::wstring path;
        if (!PickImageFile(path))
            return;
        SceneLayer l;
        l.kind        = LayerKind::Image;
        l.source      = path;
        l.target.name = FileNameOf(path);
        AddMediaLayer(l);
        SaveScenes();
    };
    a.text = []
    {
        std::wstring text = L"سأعود قريباً";
        if (!InputBox(L"النص", text, false, false, 200) || text.empty())
            return;
        SceneLayer l;
        l.kind        = LayerKind::Text;
        l.source      = text;
        l.target.name = text;
        l.background  = true;
        AddMediaLayer(l);
        SaveScenes();
    };
    a.appAudio = [] { ShowAddAppAudioMenu({}); };
    g_panels.Open(std::make_unique<AddSourcePanel>(g_hwnd, std::move(a)));
}

// =============================================================
//  المشاهد: إضافة، تبديل، تسمية، حذف، وحفظ بملف
// =============================================================
std::wstring ScenesPath()
{
    PWSTR appData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
    {
        path = std::wstring(appData) + L"\\ScreenRecorder";
        CoTaskMemFree(appData);
        CreateDirectoryW(path.c_str(), nullptr);
        path += L"\\scenes.txt";
    }
    return path;
}

// منرجّع طبقات المشهد الشغّال لمكانها بالقائمة
void StoreActiveScene()
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    if (g_activeScene >= 0 && g_activeScene < (int)g_scenes.size())
    {
        g_scenes[g_activeScene].layers = g_scene.layers;
        g_scenes[g_activeScene].apps   = g_appMixer.Entries();
    }
}

// الشاشة اللي عم تتسجّل (لمؤشر التسجيل): أكبر شاشة أو نافذة ظاهرة بالمشهد
// (الشاشة نفسها، أو الشاشة اللي عليها النافذة). كل ثانية منحسبها من جديد
HMONITOR RecordedMonitor()
{
    static HMONITOR cached = nullptr;
    static int      lastTick = -100;
    if (g_tick - lastTick < 30)
        return cached;
    lastTick = g_tick;
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    double best = 0;
    HMONITOR found = nullptr;
    for (const SceneLayer& l : g_scene.layers)
    {
        if (l.kind != LayerKind::Capture || !l.visible || l.lost || l.w * l.h <= best)
            continue;
        HMONITOR m = nullptr;
        if (l.target.isMonitor)
        {
            const POINT c{ (l.target.rect.left + l.target.rect.right) / 2, (l.target.rect.top + l.target.rect.bottom) / 2 };
            m = MonitorFromPoint(c, MONITOR_DEFAULTTONULL);
        }
        else if (l.target.hwnd && IsWindow(l.target.hwnd))
            m = MonitorFromWindow(l.target.hwnd, MONITOR_DEFAULTTONULL);
        if (m)
        {
            best  = l.w * l.h;
            found = m;
        }
    }
    cached = found;          // فاضي = الرئيسية
    return cached;
}

// اختصارات المشاهد: كل ما تغيّرت المشاهد منبعت اللائحة لمدير الاختصارات
void SyncSceneHotkeys()
{
    std::vector<std::wstring>  names;
    std::vector<HotkeyBinding> keys;
    for (const SceneData& s : g_scenes)
    {
        names.push_back(s.name);
        keys.push_back(s.hotkey);
    }
    g_hotkeys.SetScenes(names, keys);
    if (g_hwnd)
        g_hotkeys.Apply(g_hwnd);
}

// والعكس: بعد ما تغيّر اختصار بالنافذة منرجّعه للمشاهد (لحتى ينحفظ معها)
void SceneHotkeysFromManager()
{
    for (int i = 0; i < (int)g_scenes.size(); ++i)
        g_scenes[i].hotkey = g_hotkeys.Get(HK_COUNT + i);
}

// في ولا مشهد فيه أصوات برامج؟ (وقتها منضيف الخلّاط للتسجيل والبث)
bool AnySceneHasApps()
{
    if (g_appMixer.Count() > 0)
        return true;
    for (const SceneData& s : g_scenes)
        if (!s.apps.empty())
            return true;
    return false;
}

void SwitchScene(int index)
{
    if (index < 0 || index >= (int)g_scenes.size() || index == g_activeScene)
        return;
    StoreActiveScene();
    g_appMixer.SetEntries(g_scenes[index].apps);    // أصوات برامج المشهد الجديد
    g_appLevels.clear();
    // التسجيل بيكمّل بالمشهد الجديد فوراً (مع تلاشي ناعم إذا مفعّل)
    g_scene.ReplaceLayers(g_scenes[index].layers, TRANSITION_MS[g_transitionChoice]);
    if (TRANSITION_MS[g_transitionChoice] > 0)
        g_fadeTicks = TRANSITION_MS[g_transitionChoice] / 33 + 2;
    g_activeScene   = index;
    g_selectedLayer = 0;
    g_scroll        = 0;
    Notify(Glyph::Scene, Theme::Accent, L"المشهد: " + g_scenes[index].name);
}

std::wstring NewSceneName()
{
    for (int n = 1;; ++n)
    {
        const std::wstring name = L"المشهد " + std::to_wstring(n);
        bool used = false;
        for (const SceneData& s : g_scenes)
            if (s.name == name) { used = true; break; }
        if (!used)
            return name;
    }
}

void AddScene()
{
    StoreActiveScene();
    g_scenes.push_back({ NewSceneName(), {} });
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        g_scene.layers.clear();
    }
    g_appMixer.SetEntries({});
    g_appLevels.clear();
    SyncSceneHotkeys();
    g_activeScene   = (int)g_scenes.size() - 1;
    g_selectedLayer = 0;
}

void DuplicateScene(int index)
{
    StoreActiveScene();
    SceneData copy = g_scenes[index];
    copy.name += L" (نسخة)";
    copy.hotkey = {};                             // الاختصار ما بيتكرر
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (SceneLayer& l : copy.layers)
            l.id = g_scene.nextId++;              // أرقام جديدة للطبقات
    }
    g_scenes.insert(g_scenes.begin() + index + 1, copy);
    if (g_activeScene > index)
        ++g_activeScene;
    SyncSceneHotkeys();
    SwitchScene(index + 1);
}

void DeleteScene(int index)
{
    if (g_scenes.size() <= 1 || index < 0 || index >= (int)g_scenes.size())
        return;
    StoreActiveScene();
    const bool deletingActive = (index == g_activeScene);
    g_scenes.erase(g_scenes.begin() + index);
    SyncSceneHotkeys();
    if (index < g_activeScene)
        --g_activeScene;
    g_activeScene = std::clamp(g_activeScene, 0, (int)g_scenes.size() - 1);
    if (deletingActive)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        g_scene.layers = g_scenes[g_activeScene].layers;
        g_selectedLayer = 0;
        g_appMixer.SetEntries(g_scenes[g_activeScene].apps);
        g_appLevels.clear();
    }
}

// ---------- الحفظ بملف (لحتى ترجع المشاهد لما تفتح البرنامج) ----------
std::wstring Clean(std::wstring s)       // ممنوع tab وسطر جديد جوّا القيم
{
    for (wchar_t& c : s)
        if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
    return s;
}

void SaveScenes()
{
    StoreActiveScene();
    const std::wstring path = ScenesPath();
    FILE* f = nullptr;
    if (path.empty() || _wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f)
        return;
    fwprintf(f, L"A\t%d\n", g_activeScene);
    for (const SceneData& s : g_scenes)
    {
        fwprintf(f, L"S\t%ls\t%u\t%u\n", Clean(s.name).c_str(), s.hotkey.mods, s.hotkey.vk);
        for (const SceneLayer& l : s.layers)
        {
            if (l.kind != LayerKind::Capture)       // كاميرا / صورة / نص
            {
                fwprintf(f, L"M\t%d\t%.2f\t%.2f\t%.2f\t%.2f\t%d\t%d\t%d\t%u\t%d\t%ls\t%ls\t%.5f\t%.5f\t%.5f\t%.5f\n",
                         (int)l.kind, l.x, l.y, l.w, l.h, l.visible ? 1 : 0, l.shape, l.mirror ? 1 : 0,
                         l.color, l.background ? 1 : 0, Clean(l.source).c_str(), Clean(l.target.name).c_str(),
                         l.cropL, l.cropT, l.cropR, l.cropB);
                continue;
            }
            const RECT& r = l.target.rect;
            fwprintf(f, L"L\t%lc\t%ld\t%ld\t%ld\t%ld\t%.2f\t%.2f\t%.2f\t%.2f\t%d\t%ls\t%ls\t%ls\t%.5f\t%.5f\t%.5f\t%.5f\n",
                     l.target.isMonitor ? L'm' : L'w', r.left, r.top, r.right, r.bottom,
                     l.x, l.y, l.w, l.h, l.visible ? 1 : 0,
                     Clean(l.target.className).c_str(), Clean(l.target.exePath).c_str(), Clean(l.target.name).c_str(),
                     l.cropL, l.cropT, l.cropR, l.cropB);
        }
        for (const AppAudioEntry& a : s.apps)       // أصوات البرامج
            fwprintf(f, L"P\t%d\t%d\t%ls\t%ls\n", (int)std::lround(a.volume * 100), a.muted ? 1 : 0,
                     Clean(a.exePath).c_str(), Clean(a.name).c_str());
    }
    fclose(f);
}

std::vector<std::wstring> SplitTabs(const std::wstring& line)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;)
    {
        const size_t tab = line.find(L'\t', start);
        parts.push_back(line.substr(start, tab == std::wstring::npos ? std::wstring::npos : tab - start));
        if (tab == std::wstring::npos) break;
        start = tab + 1;
    }
    return parts;
}

// القص المحفوظ (بآخر السطر، 4 أرقام). الملفات القديمة ما فيها: بيضل صفر
void ReadCrop(SceneLayer& l, const std::vector<std::wstring>& p, size_t start)
{
    if (p.size() < start + 4)
        return;
    l.cropL = std::clamp(_wtof(p[start].c_str()), 0.0, 0.95);
    l.cropT = std::clamp(_wtof(p[start + 1].c_str()), 0.0, 0.95);
    l.cropR = std::clamp(_wtof(p[start + 2].c_str()), 0.0, 0.95 - l.cropL);
    l.cropB = std::clamp(_wtof(p[start + 3].c_str()), 0.0, 0.95 - l.cropT);
}

bool LoadScenes()
{
    const std::wstring path = ScenesPath();
    FILE* f = nullptr;
    if (path.empty() || _wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f)
        return false;

    const std::vector<CaptureTarget> targets = ListCaptureTargets(g_hwnd);
    int active = 0;
    wchar_t buf[4096];
    while (fgetws(buf, 4096, f))
    {
        std::wstring line = buf;
        while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r'))
            line.pop_back();
        const std::vector<std::wstring> p = SplitTabs(line);
        if (p.empty())
            continue;

        if (p[0] == L"A" && p.size() >= 2)
            active = _wtoi(p[1].c_str());
        else if (p[0] == L"S" && p.size() >= 2)
        {
            g_scenes.push_back({ p[1], {} });
            if (p.size() >= 4)                     // اختصار المشهد (الملفات القديمة ما فيها)
                g_scenes.back().hotkey = { (UINT)wcstoul(p[2].c_str(), nullptr, 10) & 0xF,
                                           (UINT)wcstoul(p[3].c_str(), nullptr, 10) & 0xFF };
        }
        else if (p[0] == L"M" && p.size() >= 13 && !g_scenes.empty())
        {
            SceneLayer l;
            l.id          = g_scene.nextId++;
            l.kind        = (LayerKind)std::clamp(_wtoi(p[1].c_str()), 1, 3);
            l.x           = _wtof(p[2].c_str());
            l.y           = _wtof(p[3].c_str());
            l.w           = _wtof(p[4].c_str());
            l.h           = _wtof(p[5].c_str());
            l.visible     = p[6] == L"1";
            l.shape       = std::clamp(_wtoi(p[7].c_str()), 0, 2);
            l.mirror      = p[8] == L"1";
            l.color       = (unsigned)wcstoul(p[9].c_str(), nullptr, 10);
            l.background  = p[10] == L"1";
            l.source      = p[11];
            l.target.name = p[12];
            l.target.isMonitor = false;
            ReadCrop(l, p, 13);
            if (l.w > 1 && l.h > 1)
                g_scenes.back().layers.push_back(l);
        }
        else if (p[0] == L"P" && p.size() >= 5 && !g_scenes.empty())   // صوت برنامج
        {
            AppAudioEntry a;
            a.volume  = std::clamp(_wtoi(p[1].c_str()), 0, 200) / 100.0f;
            a.muted   = p[2] == L"1";
            a.exePath = p[3];
            a.name    = p[4];
            if (!a.exePath.empty())
                g_scenes.back().apps.push_back(a);
        }
        else if (p[0] == L"L" && p.size() >= 14 && !g_scenes.empty())
        {
            SceneLayer l;
            l.id      = g_scene.nextId++;
            l.x       = _wtof(p[6].c_str());
            l.y       = _wtof(p[7].c_str());
            l.w       = _wtof(p[8].c_str());
            l.h       = _wtof(p[9].c_str());
            l.visible = p[10] == L"1";
            CaptureTarget& t = l.target;
            t.isMonitor = (p[1] == L"m");
            t.rect      = { _wtol(p[2].c_str()), _wtol(p[3].c_str()), _wtol(p[4].c_str()), _wtol(p[5].c_str()) };
            t.className = p[11];
            t.exePath   = p[12];
            t.name      = p[13];
            ReadCrop(l, p, 14);
            if (t.isMonitor)
            {
                // نفس الشاشة إذا لسا موصولة، وإلا الرئيسية
                const CaptureTarget* match = nullptr;
                for (const CaptureTarget& c : targets)
                    if (c.isMonitor && EqualRect(&c.rect, &t.rect)) match = &c;
                if (!match)
                    for (const CaptureTarget& c : targets)
                        if (c.isMonitor && c.isPrimary) match = &c;
                if (match)
                    t = *match;
            }
            else
            {
                t.hwnd = FindMatchingWindow(t, g_hwnd);    // إذا مش مفتوحة بتضل "مغلقة" لحتى تنفتح
                l.lost = (t.hwnd == nullptr);
            }
            if (l.w > 1 && l.h > 1)
                g_scenes.back().layers.push_back(l);
        }
    }
    fclose(f);

    if (g_scenes.empty())
        return false;
    g_activeScene = std::clamp(active, 0, (int)g_scenes.size() - 1);
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    g_scene.layers = g_scenes[g_activeScene].layers;
    return true;
}

// النوافذ المغلقة: إذا رجعت انفتحت منرجّع نربطها لحالها
void ReconnectLostWindows()
{
    auto fix = [](std::vector<SceneLayer>& layers)
    {
        for (SceneLayer& l : layers)
            if (l.kind == LayerKind::Capture && !l.target.isMonitor && !IsWindow(l.target.hwnd))
                if (HWND h = FindMatchingWindow(l.target, g_hwnd))
                {
                    l.target.hwnd = h;
                    l.lost = false;
                }
    };
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        fix(g_scene.layers);
    }
    for (int i = 0; i < (int)g_scenes.size(); ++i)
        if (i != g_activeScene)
            fix(g_scenes[i].layers);
}

// =============================================================
//  الإعدادات: الإطارات، معدّل البت، المشفّر
// =============================================================
UINT32 AutoFps()
{
    // سؤال ويندوز عن التردد بطيء شوي، فمنحفظ الجواب ومنعيد السؤال كل ثانيتين
    static UINT32 cached = 60;
    static int    lastTick = -1000;
    if (g_tick - lastTick < 60)
        return cached;
    lastTick = g_tick;

    CaptureTarget probe;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        probe.rect = { 0, 0, 1, 1 };
        for (const SceneLayer& l : g_scene.layers)
            if (l.kind == LayerKind::Capture)
            {
                probe = l.target;
                break;
            }
    }
    const UINT32 hz = GetRefreshRate(probe);
    cached = hz >= 59 ? 60 : std::max<UINT32>(24, hz);   // أقصى شي 60
    return cached;
}

UINT32 CurrentFps()
{
    return g_fpsChoice == 0 ? AutoFps() : FPS_OPTIONS[g_fpsChoice];
}

// معدّل البت = العرض × الطول × الإطارات × (بت لكل بكسل حسب الجودة)
UINT32 CurrentBitrate()
{
    if (g_quality == 3)
        return g_customMbps * 1'000'000;
    int w, h;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        w = g_scene.canvasW;
        h = g_scene.canvasH;
    }
    double bps = (double)w * h * CurrentFps() * QUALITY_BPP[g_quality];
    bps = std::clamp(bps, 1'000'000.0, 150'000'000.0);
    return (UINT32)(std::round(bps / 500'000.0) * 500'000.0);
}

std::wstring MbpsText(UINT32 bps)
{
    wchar_t buf[32];
    const double mbps = bps / 1'000'000.0;
    if (std::fabs(mbps - std::round(mbps)) < 0.05) swprintf_s(buf, L"%.0f", mbps);
    else                                           swprintf_s(buf, L"%.1f", mbps);
    return buf;
}

void ChangeBitrate(int direction)
{
    int mbps = (int)std::lround(CurrentBitrate() / 1'000'000.0);
    const int step = mbps >= 20 ? 5 : 1;
    mbps = std::clamp(mbps + direction * step, 1, 150);
    g_customMbps = (UINT32)mbps;
    g_quality = 3;
}

void RenameScene(int index)
{
    std::wstring name = g_scenes[index].name;
    if (InputBox(L"اسم المشهد", name))
    {
        // منشيل المسافات من الأطراف
        const size_t a = name.find_first_not_of(L' ');
        const size_t b = name.find_last_not_of(L' ');
        name = (a == std::wstring::npos) ? L"" : name.substr(a, b - a + 1);
        if (!name.empty())
            g_scenes[index].name = name;
        SyncSceneHotkeys();       // الاسم الجديد بنافذة الاختصارات
        SaveScenes();
    }
}

void ShowSceneMenu(int index, POINT pt)
{
    std::vector<std::wstring> items = { L"إعادة التسمية", L"تكرار المشهد" };
    if (g_scenes.size() > 1)
    {
        items.push_back(L"");
        items.push_back(L"حذف المشهد");
    }
    const int cmd = ShowMenu(items, -1, pt);
    if (cmd == 0) RenameScene(index);
    if (cmd == 1) DuplicateScene(index);
    if (cmd == 3) DeleteScene(index);
    if (cmd >= 0) SaveScenes();
}

// اسم المشفّر اللي رح ينستعمل حسب الاختيار
std::wstring EncoderLabel()
{
    if (g_encoderMode == 0) return L"تلقائي (أفضل مشفّر متاح)";
    if (g_encoderMode == 2) return L"المعالج (Microsoft H.264)";
    return (g_encoderName.empty() ? L"بطاقة الرسومات" : g_encoderName);
}

void ShowEncoderMenu(POINT pt)
{
    // القائمة: تلقائي + كل بطاقات الرسومات + المعالج
    std::vector<std::wstring> items = { L"تلقائي (أفضل مشفّر متاح)" };
    std::vector<int> hwIndex;
    for (size_t i = 0; i < g_encoders.size(); ++i)
        if (g_encoders[i].hardware)
        {
            items.push_back(g_encoders[i].name + L"  (بطاقة الرسومات)");
            hwIndex.push_back((int)i);
        }
    items.push_back(L"المعالج (Microsoft H.264)");

    int checked = 0;
    if (g_encoderMode == 2) checked = (int)items.size() - 1;
    if (g_encoderMode == 1)
        for (size_t k = 0; k < hwIndex.size(); ++k)
            if (g_encoders[hwIndex[k]].name == g_encoderName) checked = (int)k + 1;

    const int pick = ShowMenu(items, checked, pt);
    if (pick < 0) return;
    if (pick == 0)                          { g_encoderMode = 0; g_encoderName.clear(); }
    else if (pick == (int)items.size() - 1) { g_encoderMode = 2; g_encoderName.clear(); }
    else                                    { g_encoderMode = 1; g_encoderName = g_encoders[hwIndex[pick - 1]].name; }
    SaveSettings();
}

void SaveSettings()
{
    if (g_settingsPath.empty())
        return;
    auto put = [](const wchar_t* key, const std::wstring& v)
    {
        WritePrivateProfileStringW(L"Settings", key, v.c_str(), g_settingsPath.c_str());
    };
    put(L"Canvas",      std::to_wstring(g_canvasChoice));
    put(L"FpsChoice",   std::to_wstring(g_fpsChoice));
    put(L"Quality",     std::to_wstring(g_quality));
    put(L"CustomMbps",  std::to_wstring(g_customMbps));
    put(L"EncoderMode", std::to_wstring(g_encoderMode));
    put(L"EncoderName", g_encoderName);
    put(L"CrashSafe",   g_crashSafe ? L"1" : L"0");
    put(L"SeparateTracks", g_separateTracks ? L"1" : L"0");
    put(L"OverlayCorner",  std::to_wstring(g_overlayCorner));
    put(L"CloseToTray",    g_closeToTray ? L"1" : L"0");
    put(L"Toasts",         g_toastsOn ? L"1" : L"0");
    put(L"OverlayOverApp", g_overlayOverApp ? L"1" : L"0");
    put(L"TrayHintShown",  g_trayHintShown ? L"1" : L"0");
    put(L"SystemAudio", g_systemOn ? L"1" : L"0");
    put(L"Microphone",  g_micOn ? L"1" : L"0");
    put(L"SystemDevice", g_systemAudio.DeviceId());
    put(L"MicDevice",    g_micAudio.DeviceId());
    put(L"SystemVolume", std::to_wstring((int)std::lround(g_systemAudio.Volume() * 100)));
    put(L"MicVolume",    std::to_wstring((int)std::lround(g_micAudio.Volume() * 100)));
    put(L"SystemMuted",  g_systemAudio.IsMuted() ? L"1" : L"0");
    put(L"MicMuted",     g_micAudio.IsMuted() ? L"1" : L"0");
    put(L"StreamRes",    std::to_wstring(g_streamRes));
    put(L"StreamKbps",   std::to_wstring(g_streamKbps));
    put(L"VerticalMode", std::to_wstring(g_verticalMode));
    put(L"ReplayChoice", std::to_wstring(g_replaySet.seconds));
    put(L"ReplayOn",     g_replayWanted ? L"1" : L"0");
    put(L"ReplaySource", std::to_wstring(g_replaySet.source));
    put(L"ReplayMonitor", std::to_wstring(g_replaySet.monitor));
    put(L"ReplayRes",    std::to_wstring(g_replaySet.res));
    put(L"ReplayFps",    std::to_wstring(g_replaySet.fps));
    put(L"ReplayQuality", std::to_wstring(g_replaySet.quality));
    put(L"ReplaySystem", g_replaySet.system ? L"1" : L"0");
    put(L"ReplayMic",    g_replaySet.mic ? L"1" : L"0");
    put(L"ReplayAuto",   g_replaySet.autoStart ? L"1" : L"0");
    put(L"ReplayFolder", g_replaySet.folder);
    put(L"RecordFolder", g_folder);
    put(L"Transition",   std::to_wstring(g_transitionChoice));
    put(L"MicNoise",     g_micAudio.Filters().noise ? L"1" : L"0");
    put(L"MicGate",      g_micAudio.Filters().gate ? L"1" : L"0");
    put(L"MicLeveler",   g_micAudio.Filters().leveler ? L"1" : L"0");
    // إعدادات الفلاتر (منحفظها أرقام صحيحة: الديسيبل والملّي ثانية متل ما هي، والنسب ×100 أو ×10)
    const MicFilters& mf = g_micAudio.Filters();
    put(L"MicNoiseStrength", std::to_wstring((int)std::lround(mf.noiseStrength * 100)));
    put(L"MicGateOpenDb",    std::to_wstring((int)std::lround(mf.gateOpenDb.load())));
    put(L"MicGateHoldMs",    std::to_wstring((int)std::lround(mf.gateHoldMs.load())));
    put(L"MicGateCloseMs",   std::to_wstring((int)std::lround(mf.gateCloseMs.load())));
    put(L"MicLevelGainDb",   std::to_wstring((int)std::lround(mf.levelGainDb.load())));
    put(L"MicLevelRatio",    std::to_wstring((int)std::lround(mf.levelRatio * 10)));
}

void LoadSettings()
{
    PWSTR appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
    {
        const std::wstring dir = std::wstring(appData) + L"\\ScreenRecorder";
        CoTaskMemFree(appData);
        CreateDirectoryW(dir.c_str(), nullptr);
        g_settingsPath = dir + L"\\settings.ini";
    }
    if (g_settingsPath.empty())
        return;
    auto getInt = [](const wchar_t* key, int def)
    {
        return (int)GetPrivateProfileIntW(L"Settings", key, def, g_settingsPath.c_str());
    };
    auto getStr = [](const wchar_t* key)
    {
        wchar_t buf[512] = {};
        GetPrivateProfileStringW(L"Settings", key, L"", buf, 512, g_settingsPath.c_str());
        return std::wstring(buf);
    };
    g_canvasChoice = std::clamp(getInt(L"Canvas", 0), 0, 3);
    g_fpsChoice    = std::clamp(getInt(L"FpsChoice", 0), 0, 3);
    g_quality      = std::clamp(getInt(L"Quality", 2), 0, 3);
    g_customMbps   = (UINT32)std::clamp(getInt(L"CustomMbps", 12), 1, 150);
    g_encoderMode  = std::clamp(getInt(L"EncoderMode", 0), 0, 2);
    g_encoderName  = getStr(L"EncoderName");
    g_crashSafe    = getInt(L"CrashSafe", 1) != 0;
    g_separateTracks = getInt(L"SeparateTracks", 0) != 0;
    g_overlayCorner  = std::clamp(getInt(L"OverlayCorner", 1), 0, 4);
    g_closeToTray    = getInt(L"CloseToTray", 1) != 0;
    g_toastsOn       = getInt(L"Toasts", 1) != 0;
    g_overlayOverApp = getInt(L"OverlayOverApp", 1) != 0;
    g_trayHintShown  = getInt(L"TrayHintShown", 0) != 0;
    g_hotkeys.Load(g_settingsPath);
    g_systemOn     = getInt(L"SystemAudio", 1) != 0;
    g_micOn        = getInt(L"Microphone", 0) != 0;
    g_systemAudio.SetDeviceId(getStr(L"SystemDevice"));
    g_micAudio.SetDeviceId(getStr(L"MicDevice"));
    g_systemAudio.SetVolume(std::clamp(getInt(L"SystemVolume", 100), 0, 200) / 100.0f);
    g_micAudio.SetVolume(std::clamp(getInt(L"MicVolume", 100), 0, 200) / 100.0f);
    g_systemAudio.SetMuted(getInt(L"SystemMuted", 0) != 0);
    g_micAudio.SetMuted(getInt(L"MicMuted", 0) != 0);
    g_streamRes  = std::clamp(getInt(L"StreamRes", 1), 0, 2);
    g_streamKbps = (UINT32)std::clamp(getInt(L"StreamKbps", 6000), 1000, 20000);
    g_verticalMode     = std::clamp(getInt(L"VerticalMode", 0), 0, 1);
    g_replaySet.seconds  = std::clamp(getInt(L"ReplayChoice", 1), 0, 3);
    g_replayWanted       = getInt(L"ReplayOn", 0) != 0;
    g_replaySet.source   = std::clamp(getInt(L"ReplaySource", 0), 0, 1);
    g_replaySet.monitor  = std::clamp(getInt(L"ReplayMonitor", 0), 0, 3);
    g_replaySet.res      = std::clamp(getInt(L"ReplayRes", 1), 0, 2);
    g_replaySet.fps      = std::clamp(getInt(L"ReplayFps", 1), 0, 1);
    g_replaySet.quality  = std::clamp(getInt(L"ReplayQuality", 1), 0, 2);
    g_replaySet.system   = getInt(L"ReplaySystem", 1) != 0;
    g_replaySet.mic      = getInt(L"ReplayMic", 1) != 0;
    g_replaySet.autoStart = getInt(L"ReplayAuto", g_replayWanted ? 1 : 0) != 0;   // قبل: كانت بترجع إذا كانت شغّالة
    g_replaySet.folder   = getStr(L"ReplayFolder");
    {
        const std::wstring rf = getStr(L"RecordFolder");
        if (!rf.empty())
            g_folder = rf;
    }
    g_transitionChoice = std::clamp(getInt(L"Transition", 1), 0, 2);
    g_micAudio.Filters().noise   = getInt(L"MicNoise", 0) != 0;
    g_micAudio.Filters().gate    = getInt(L"MicGate", 0) != 0;
    g_micAudio.Filters().leveler = getInt(L"MicLeveler", 0) != 0;
    {
        MicFilters& mf = g_micAudio.Filters();
        // GetPrivateProfileInt ما بيقرأ الأرقام السالبة صح، فمنقرأها كنص
        auto getF = [&](const wchar_t* key, float def)
        {
            const std::wstring v = getStr(key);
            return v.empty() ? def : (float)_wtoi(v.c_str());
        };
        mf.noiseStrength = std::clamp(getF(L"MicNoiseStrength", MicFilters::DEF_NOISE_STRENGTH * 100) / 100.0f, 0.0f, 2.0f);
        mf.gateOpenDb    = std::clamp(getF(L"MicGateOpenDb", MicFilters::DEF_GATE_OPEN_DB), -60.0f, -10.0f);
        mf.gateHoldMs    = std::clamp(getF(L"MicGateHoldMs", MicFilters::DEF_GATE_HOLD_MS), 0.0f, 1000.0f);
        mf.gateCloseMs   = std::clamp(getF(L"MicGateCloseMs", MicFilters::DEF_GATE_CLOSE_MS), 20.0f, 1000.0f);
        mf.levelGainDb   = std::clamp(getF(L"MicLevelGainDb", MicFilters::DEF_LEVEL_GAIN_DB), 0.0f, 20.0f);
        mf.levelRatio    = std::clamp(getF(L"MicLevelRatio", MicFilters::DEF_LEVEL_RATIO * 10) / 10.0f, 1.5f, 8.0f);
    }
    g_replayBuffer->SetSeconds(REPLAY_SECONDS[g_replaySet.seconds]);
}

// =============================================================
//  الصوت
// =============================================================
void SetAudioEnabled(AudioSource& src, bool& flag, bool on)
{
    if (Busy())
        return;                     // ما منغيّر الصوت بنص التسجيل أو البث
    flag = on;
    if (on) src.Start();
    else    src.Stop();
    SaveSettings();
    RestartReplayIfActive();
}

// قائمة اختيار الجهاز (سماعات أو ميكروفون)
void ShowDeviceMenu(AudioSource& src, bool on, AudioSource::Kind kind, POINT pt)
{
    if (Busy())
        return;
    const std::vector<AudioDevice> devices = AudioSource::ListDevices(kind);
    std::vector<std::wstring> items = { L"الجهاز الافتراضي في ويندوز" };
    int checked = src.DeviceId().empty() ? 0 : -1;
    for (size_t i = 0; i < devices.size(); ++i)
    {
        items.push_back(devices[i].name);
        if (devices[i].id == src.DeviceId())
            checked = (int)i + 1;
    }
    const int pick = ShowMenu(items, checked, pt);
    if (pick < 0)
        return;
    src.SetDeviceId(pick == 0 ? L"" : devices[pick - 1].id);
    if (on)                     // منعيد تشغيله على الجهاز الجديد
    {
        src.Stop();
        src.Start();
    }
    SaveSettings();
    RestartReplayIfActive();
}

// من مستوى الصوت (0..1) لطول الشريط، بالـ dB لأن الأذن هيك بتسمع
float LevelToMeter(float peak)
{
    if (peak <= 0.00001f)
        return 0.0f;
    const float db = 20.0f * std::log10(peak);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

// =============================================================
//  المعاينة: منرسم المشهد كلّه بحجم صغير
// =============================================================
void UpdatePreview()
{
    if (!g_rt)
        return;

    const float scale = g_dpi / 96.0f;
    const int pw = std::max(2, std::min(1280, (int)((g_previewDst.right - g_previewDst.left) * scale)));
    const int ph = std::max(2, std::min(720,  (int)((g_previewDst.bottom - g_previewDst.top) * scale)));
    if (pw < 8 || ph < 8)
        return;

    if (pw != g_previewW || ph != g_previewH)
    {
        g_previewPixels.assign((size_t)pw * ph * 4, 0);
        g_previewW = pw;
        g_previewH = ph;
        g_previewBitmap.Reset();
    }

    int cw, ch;
    std::vector<SceneLayer> layers;
    std::vector<int> lost;
    ComposeParams cp;
    cp.out = g_previewPixels.data(); cp.outW = pw; cp.outH = ph; cp.stride = pw * 4; cp.lostIds = &lost;
    g_scene.Snapshot(cw, ch, layers, cp.fade);
    g_previewCompositor.Compose(layers, cw, ch, cp);

    // منعلّم الطبقات اللي نوافذها انسكّرت
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (SceneLayer& l : g_scene.layers)
            l.lost = std::find(lost.begin(), lost.end(), l.id) != lost.end();
    }

    if (!g_previewBitmap)
    {
        const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        g_rt->CreateBitmap(D2D1::SizeU(pw, ph), g_previewPixels.data(), pw * 4, props, &g_previewBitmap);
    }
    else
    {
        g_previewBitmap->CopyFromMemory(nullptr, g_previewPixels.data(), pw * 4);
    }
    g_previewValid = (g_previewBitmap != nullptr);
}

// =============================================================
//  التسجيل
// =============================================================
std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// إشعار بزاوية الشاشة (بيبيّن لما البرنامج مش قدامك، متل وقت اللعب)
void Notify(wchar_t glyph, D2D1_COLOR_F color, const std::wstring& title, const std::wstring& sub)
{
    if (g_toastsOn)
        g_overlay.Toast(glyph, color, title, sub);
}

void ShowMessage(const std::wstring& text, bool isError)
{
    g_message        = text;
    g_messageIsError = isError;
    if (isError && !text.empty())        // الأخطاء كمان بتطلع إشعار (مثلاً وانت باللعبة)
        Notify(Glyph::Warning, Theme::Red, L"حدث خطأ", text);
}

// نص الاختصار (مثلاً "F9") للإشعارات: "اضغط F9 لـ..."
std::wstring KeyHint(int action, const std::wstring& what)
{
    const std::wstring k = HotkeyManager::Text(g_hotkeys.Get(action));
    return k.empty() ? std::wstring() : L"اضغط " + LtrText(k) + L" " + what;
}

// -------------------------------------------------------------
//  أماكن الحفظ
// -------------------------------------------------------------
// مجلد الإعادات: اللي اخترته، وإلا "Replays" جوّا مجلد التسجيلات
std::wstring ReplayFolder()
{
    return g_replaySet.folder.empty() ? g_folder + L"\\Replays" : g_replaySet.folder;
}

// صندوق ويندوز لاختيار مجلد
bool PickFolder(std::wstring& path, const wchar_t* title)
{
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(title);
    CreateDirectoryW(path.c_str(), nullptr);
    ComPtr<IShellItem> start;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&start))))
        dlg->SetFolder(start.Get());
    if (FAILED(dlg->Show(g_hwnd)))
        return false;                    // انلغى
    ComPtr<IShellItem> item;
    PWSTR chosen = nullptr;
    if (FAILED(dlg->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen)))
        return false;
    path = chosen;
    CoTaskMemFree(chosen);
    return true;
}

void OpenFolderPath(const std::wstring& folder)
{
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// -------------------------------------------------------------
//  التشغيل مع ويندوز (مفتاح Run بالريجستري، مع --tray لحتى يبدأ مخفي)
// -------------------------------------------------------------
const wchar_t RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t RUN_NAME[] = L"ScreenRecorder";

bool IsRunAtStartup()
{
    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_NAME, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void SetRunAtStartup(bool on)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (on)
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --tray";
        RegSetValueExW(key, RUN_NAME, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    }
    else
        RegDeleteValueW(key, RUN_NAME);
    RegCloseKey(key);
}

// الشاشات للإعادة (الرئيسية أول)
std::vector<CaptureTarget> ReplayMonitors()
{
    std::vector<CaptureTarget> mons;
    for (const CaptureTarget& t : ListCaptureTargets(g_hwnd))
        if (t.isMonitor)
            mons.push_back(t);
    std::stable_sort(mons.begin(), mons.end(),
                     [](const CaptureTarget& a, const CaptureTarget& b) { return a.isPrimary && !b.isPrimary; });
    return mons;
}

void StartRecording()
{
    if (g_recorder.IsRecording())
        return;
    {
        // مشهد فاضي = فيديو أسود: منبّهك بدل ما نسجّل لا شي
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        bool any = false;
        for (const SceneLayer& l : g_scene.layers)
            if (l.visible) { any = true; break; }
        if (!any)
        {
            g_message = L"المشهد فارغ — أضف مصدرًا (مثل الشاشة كاملة) قبل التسجيل";
            g_messageIsError = true;
            Notify(Glyph::Warning, Theme::Yellow, L"المشهد فارغ", L"أضف مصدرًا قبل التسجيل");
            return;
        }
    }

    CreateDirectoryW(g_folder.c_str(), nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, L"Recording_%04d-%02d-%02d_%02d-%02d-%02d.mp4",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    RecordOptions opt;
    opt.scene   = &g_scene;
    opt.path    = g_folder + L"\\" + name;
    opt.notify  = g_hwnd;
    opt.fps     = CurrentFps();
    opt.bitrate = CurrentBitrate();
    opt.encoder = (EncoderMode)g_encoderMode;
    opt.crashSafe = g_crashSafe;
    opt.separateTracks = g_separateTracks;
    if (g_systemOn && g_systemAudio.IsRunning()) opt.audio.push_back(&g_systemAudio);
    if (g_micOn && g_micAudio.IsRunning())       opt.audio.push_back(&g_micAudio);
    if (AnySceneHasApps())                       opt.audio.push_back(&g_appMixer.Output());

    const HRESULT hr = g_recorder.Start(opt);
    if (SUCCEEDED(hr))
    {
        g_lastFile = opt.path;
        // إذا اخترت بطاقة رسومات معيّنة وويندوز استعمل غيرها منخبّرك
        if (g_encoderMode == 1 && !g_encoderName.empty() && g_recorder.EncoderName() != g_encoderName)
            ShowMessage(L"تعذّر استخدام " + g_encoderName + L" فاستُخدم " + g_recorder.EncoderName(), true);
        else
            ShowMessage(L"", false);
        Notify(Glyph::Record, Theme::Red, L"بدأ التسجيل", KeyHint(HK_RECORD, L"للإيقاف"));
    }
    else
    {
        wchar_t msg[128];
        swprintf_s(msg, L"تعذّر بدء التسجيل (رمز الخطأ 0x%08X)", (unsigned)hr);
        ShowMessage(msg, true);
    }
    UpdateAppIcon();
}

void FinishRecording(HRESULT hr)
{
    if (SUCCEEDED(hr))
    {
        ShowMessage(L"✓ تم الحفظ: " + FileNameOf(g_lastFile), false);
        Notify(Glyph::Check, Theme::Green, L"تم حفظ التسجيل", FileNameOf(g_lastFile));
    }
    else
    {
        wchar_t msg[128];
        swprintf_s(msg, L"حدث خطأ أثناء التسجيل (0x%08X)", (unsigned)hr);
        ShowMessage(msg, true);
    }
    UpdateAppIcon();
}

void StopRecording()
{
    if (!g_recorder.IsRecording())
        return;
    FinishRecording(g_recorder.Stop());
}

void ToggleRecording()
{
    if (g_recorder.IsRecording()) StopRecording();
    else                          StartRecording();
}

void TogglePause()
{
    if (g_recorder.IsRecording())
    {
        g_recorder.SetPaused(!g_recorder.IsPaused());
        if (g_recorder.IsPaused())
            Notify(Glyph::Pause, Theme::Yellow, L"توقّف التسجيل مؤقتًا", KeyHint(HK_PAUSE, L"للاستئناف"));
        else
            Notify(Glyph::Play, Theme::Red, L"استُؤنف التسجيل");
    }
    UpdateAppIcon();
}


// =============================================================
//  البث المباشر: الوجهات (يوتيوب، تيك توك...)
// =============================================================

// مفتاح البث سرّي: منشفّره بـ DPAPI (بس حسابك بويندوز بيقدر يفكّه)
std::wstring ProtectText(const std::wstring& plain)
{
    if (plain.empty())
        return L"";
    DATA_BLOB in{ (DWORD)(plain.size() * sizeof(wchar_t)), (BYTE*)plain.data() }, out{};
    if (!CryptProtectData(&in, L"stream key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return L"";
    DWORD len = 0;
    CryptBinaryToStringW(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len);
    std::wstring b64(len, L'\0');
    CryptBinaryToStringW(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &len);
    b64.resize(len);
    LocalFree(out.pbData);
    return b64;
}

std::wstring UnprotectText(const std::wstring& b64)
{
    if (b64.empty())
        return L"";
    DWORD len = 0;
    if (!CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr))
        return L"";
    std::vector<BYTE> bin(len);
    if (!CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, bin.data(), &len, nullptr, nullptr))
        return L"";
    DATA_BLOB in{ len, bin.data() }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return L"";
    std::wstring plain((wchar_t*)out.pbData, out.cbData / sizeof(wchar_t));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}

std::wstring TargetsPath()
{
    std::wstring path = ScenesPath();
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"" : path.substr(0, slash) + L"\\streams.txt";
}

void SaveTargets()
{
    const std::wstring path = TargetsPath();
    FILE* f = nullptr;
    if (path.empty() || _wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f)
        return;
    for (const StreamTarget& t : g_targets)
        fwprintf(f, L"T\t%d\t%d\t%ls\t%ls\t%ls\t%d\n", (int)t.platform, t.enabled ? 1 : 0,
                 Clean(t.name).c_str(), Clean(t.url).c_str(), ProtectText(t.key).c_str(), t.vertical ? 1 : 0);
    fclose(f);
}

void LoadTargets()
{
    g_targets.clear();
    const std::wstring path = TargetsPath();
    FILE* f = nullptr;
    if (path.empty() || _wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f)
        return;
    wchar_t buf[8192];
    while (fgetws(buf, 8192, f))
    {
        std::wstring line = buf;
        while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r'))
            line.pop_back();
        const std::vector<std::wstring> p = SplitTabs(line);
        if (p.size() < 6 || p[0] != L"T")
            continue;
        StreamTarget t;
        t.platform = (Platform)std::clamp(_wtoi(p[1].c_str()), 0, (int)Platform::Custom);
        t.enabled  = _wtoi(p[2].c_str()) != 0;
        t.name     = p[3];
        t.url      = p[4];
        t.key      = UnprotectText(p[5]);
        t.vertical = p.size() >= 7 && p[6] == L"1";
        g_targets.push_back(t);
    }
    fclose(f);
}

D2D1_COLOR_F PlatformColor(Platform p)
{
    switch (p)
    {
    case Platform::YouTube:  return D2D1::ColorF(0xFF0033);
    case Platform::Twitch:   return D2D1::ColorF(0x9146FF);
    case Platform::Facebook: return D2D1::ColorF(0x1877F2);
    case Platform::Kick:     return D2D1::ColorF(0x53FC18);
    case Platform::TikTok:   return D2D1::ColorF(0xFE2C55);
    default:                 return Theme::Accent;
    }
}

// أيقونات المنصات:
//  - إذا حطّيت صورة الشعار الرسمي بمجلد الأيقونات (youtube.png ...) منستعملها
//  - غير هيك منرسم أيقونة بسيطة بلون المنصة
const wchar_t* PLATFORM_FILES[] = { L"youtube", L"twitch", L"facebook", L"kick", L"tiktok", L"custom" };
constexpr int  PLATFORM_COUNT = 6;

ComPtr<IWICImagingFactory>    g_wic;
ComPtr<IWICFormatConverter>   g_logoSrc[PLATFORM_COUNT];
bool                          g_logoTried[PLATFORM_COUNT] = {};
ComPtr<ID2D1RenderTarget>     g_logoRt;                  // الرسّام اللي انعملت عليه الصور
ComPtr<ID2D1Bitmap>           g_logoBmp[PLATFORM_COUNT];

std::wstring IconsFolder()
{
    std::wstring path = ScenesPath();
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return L"";
    path = path.substr(0, slash) + L"\\icons";
    CreateDirectoryW(path.c_str(), nullptr);
    return path;
}

// بنقرا صورة الشعار مرة وحدة (إذا موجودة)
IWICFormatConverter* LogoSource(int p)
{
    if (g_logoTried[p])
        return g_logoSrc[p].Get();
    g_logoTried[p] = true;
    if (!g_wic && FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic))))
        return nullptr;
    const std::wstring folder = IconsFolder();
    if (folder.empty())
        return nullptr;
    for (const wchar_t* ext : { L".png", L".ico", L".jpg" })
    {
        const std::wstring file = folder + L"\\" + PLATFORM_FILES[p] + ext;
        if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> conv;
        if (SUCCEEDED(g_wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) &&
            SUCCEEDED(g_wic->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)))
        {
            g_logoSrc[p] = conv;
            break;
        }
    }
    return g_logoSrc[p].Get();
}

void DrawPlatformIcon(int p, const D2D1_RECT_F& r, bool enabled)
{
    if (p < 0 || p >= PLATFORM_COUNT || !g_rt)
        return;

    // 1) صورة الشعار من المجلد
    if (IWICFormatConverter* src = LogoSource(p))
    {
        if (g_logoRt.Get() != g_rt.Get())      // رسّام جديد (قائمة منبثقة مثلاً): منعيد الصور
        {
            g_logoRt = g_rt;
            for (auto& b : g_logoBmp) b.Reset();
        }
        if (!g_logoBmp[p])
            g_rt->CreateBitmapFromWicBitmap(src, nullptr, &g_logoBmp[p]);
        if (g_logoBmp[p])
        {
            const D2D1_SIZE_F bs = g_logoBmp[p]->GetSize();
            const float bw = r.right - r.left, bh = r.bottom - r.top;
            const float k  = std::min(bw / bs.width, bh / bs.height);
            const float w  = bs.width * k, h = bs.height * k;
            const float x  = r.left + (bw - w) / 2, y = r.top + (bh - h) / 2;
            g_rt->DrawBitmap(g_logoBmp[p].Get(), Rect(x, y, x + w, y + h), enabled ? 1.0f : 0.4f,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
    }

    // 2) أيقونة بسيطة بلون المنصة
    static const wchar_t* glyphs[PLATFORM_COUNT] = {
        L"\uE714",   // يوتيوب: فيديو
        L"\uE7FC",   // تويتش: ألعاب
        L"\uE716",   // فيسبوك: أشخاص
        L"\uE945",   // كيك: برق
        L"\uE8D6",   // تيك توك: موسيقى
        L"\uE774" }; // مخصّص: إنترنت
    const D2D1_COLOR_F c = PlatformColor((Platform)p);
    const float size = r.bottom - r.top;
    Fill(r, WithAlpha(c, enabled ? 1.0f : 0.35f), size * 0.26f);
    Text(glyphs[p], size >= 30 ? g_fIconBig.Get() : g_fIcon.Get(), r,
         (Platform)p == Platform::Kick ? D2D1::ColorF(0x0E1014) : Theme::White);
}

// وين بيلاقي المستخدم المفتاح (بيبيّن فوق مربع الكتابة)
std::wstring KeyPrompt(const StreamTarget& t)
{
    switch (t.platform)
    {
    case Platform::YouTube:  return L"مفتاح البث (YouTube Studio ← بث مباشر)";
    case Platform::Twitch:   return L"مفتاح البث (لوحة تحكم Twitch ← البث)";
    case Platform::Facebook: return L"مفتاح البث (Live Producer في فيسبوك)";
    case Platform::Kick:     return L"مفتاح البث (Kick ← الإعدادات ← مفتاح البث)";
    case Platform::TikTok:   return L"مفتاح البث (TikTok LIVE Studio / LIVE Center)";
    default:                 return L"مفتاح البث";
    }
}

bool EditTargetUrl(StreamTarget& t)
{
    std::wstring url = t.url;
    const std::wstring title = t.platform == Platform::Kick   ? L"عنوان الخادم (Stream URL من Kick)"
                             : t.platform == Platform::TikTok ? L"عنوان الخادم (Server URL من TikTok)"
                                                              : L"عنوان الخادم (rtmp:// أو rtmps://)";
    if (!InputBox(title, url, false, true, 512))
        return false;
    t.url = url;
    return true;
}

bool EditTargetKey(StreamTarget& t)
{
    std::wstring key = t.key;
    if (!InputBox(KeyPrompt(t), key, true, true, 1024))
        return false;
    t.key = key;
    return true;
}

void AddTarget(POINT pt)
{
    if (g_streamer.IsActive())
    {
        ShowMessage(L"أوقف البث أولاً لإضافة وجهة", true);
        return;
    }
    const Platform list[] = { Platform::YouTube, Platform::Twitch, Platform::Facebook,
                              Platform::Kick, Platform::TikTok, Platform::Custom };
    std::vector<std::wstring> items;
    std::vector<int> icons;
    for (Platform p : list)
    {
        items.push_back(p == Platform::Custom ? L"خادم مخصّص (RTMP)" : PlatformName(p));
        icons.push_back((int)p);
    }
    const int pick = ShowMenu(items, -1, pt, icons);
    if (pick < 0)
        return;

    StreamTarget t;
    t.platform = list[pick];
    t.name     = PlatformName(t.platform);
    t.url      = DefaultServer(t.platform);
    t.enabled  = true;
    t.vertical = t.platform == Platform::TikTok;      // تيك توك عمودي من الأساس
    if (t.url.empty() && !EditTargetUrl(t))
        return;
    EditTargetKey(t);              // إذا لغاه بيقدر يحطّه بعدين من القائمة
    g_targets.push_back(t);
    SaveTargets();
}

void ShowTargetMenu(int index, POINT pt)
{
    if (index < 0 || index >= (int)g_targets.size())
        return;
    if (g_streamer.IsActive())
    {
        ShowMessage(L"أوقف البث أولاً لتعديل الوجهات", true);
        return;
    }
    StreamTarget& t = g_targets[index];
    const int pick = ShowMenu({ L"مفتاح البث", L"عنوان الخادم", L"إعادة التسمية",
                                t.vertical ? L"✓   بث عمودي (9:16)" : L"      بث عمودي (9:16)", L"", L"حذف الوجهة" }, -1, pt);
    switch (pick)
    {
    case 0: if (EditTargetKey(t)) SaveTargets(); break;
    case 1: if (EditTargetUrl(t)) SaveTargets(); break;
    case 2:
    {
        std::wstring name = t.name;
        if (InputBox(L"اسم الوجهة", name) && !name.empty())
        {
            t.name = name;
            SaveTargets();
        }
        break;
    }
    case 3:
        t.vertical = !t.vertical;
        SaveTargets();
        break;
    case 5:
        g_targets.erase(g_targets.begin() + index);
        SaveTargets();
        break;
    }
}

void ToggleTarget(int index)
{
    if (index < 0 || index >= (int)g_targets.size())
        return;
    if (g_streamer.IsActive())
    {
        ShowMessage(L"أوقف البث أولاً لتغيير الوجهات", true);
        return;
    }
    g_targets[index].enabled = !g_targets[index].enabled;
    SaveTargets();
}

// دقة البث حسب الاختيار (بنفس نسبة اللوحة)
void StreamSize(UINT32& w, UINT32& h)
{
    int cw, ch;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        cw = g_scene.canvasW;
        ch = g_scene.canvasH;
    }
    w = (UINT32)cw;
    h = (UINT32)ch;
    const int wantH = g_streamRes == 1 ? 1080 : (g_streamRes == 2 ? 720 : 0);
    // اللوحة العمودية (تيك توك): منحسب على العرض بدل الطول
    const bool vertical = ch > cw;
    const int  base     = vertical ? cw : ch;
    if (wantH > 0 && base > wantH)
    {
        w = (UINT32)((double)cw * wantH / base);
        h = (UINT32)((double)ch * wantH / base);
    }
    w &= ~1u;
    h &= ~1u;
}

void StartStreaming()
{
    if (g_streamer.IsActive())
        return;

    StreamOptions opt;
    opt.scene  = &g_scene;
    opt.notify = g_hwnd;
    g_liveTargets.clear();
    for (size_t i = 0; i < g_targets.size(); ++i)
        if (g_targets[i].enabled)
        {
            opt.targets.push_back(g_targets[i]);
            g_liveTargets.push_back((int)i);
        }
    if (opt.targets.empty())
    {
        g_tab = 1;
        ShowMessage(g_targets.empty() ? L"أضف وجهة بث أولاً (يوتيوب، تيك توك...)" : L"فعّل وجهة بث واحدة على الأقل", true);
        return;
    }
    StreamSize(opt.width, opt.height);
    opt.verticalW    = g_streamRes == 2 ? 720 : 1080;
    opt.verticalH    = g_streamRes == 2 ? 1280 : 1920;
    opt.verticalMode = g_verticalMode == 0 ? VerticalMode::Crop : VerticalMode::Fit;
    opt.fps       = std::min<UINT32>(CurrentFps(), 60);
    opt.videoKbps = g_streamKbps;
    opt.encoder   = (EncoderMode)g_encoderMode;
    if (g_systemOn && g_systemAudio.IsRunning()) opt.audio.push_back(&g_systemAudio);
    if (g_micOn && g_micAudio.IsRunning())       opt.audio.push_back(&g_micAudio);
    if (AnySceneHasApps())                       opt.audio.push_back(&g_appMixer.Output());

    std::wstring error;
    if (g_streamer.Start(opt, error))
    {
        g_streamStatus.clear();
        const std::wstring to = opt.targets.size() == 1 ? L"إلى " + opt.targets[0].name
                                                        : L"إلى " + std::to_wstring(opt.targets.size()) + L" منصات";
        ShowMessage(L"بدأ البث " + to, false);
        Notify(Glyph::Live, Theme::Red, L"بدأ البث المباشر", to);
    }
    else
    {
        g_liveTargets.clear();
        g_tab = 1;
        ShowMessage(error, true);
    }
    UpdateAppIcon();
}

void StopStreaming(const std::wstring& message = L"انتهى البث")
{
    if (!g_streamer.IsActive())
        return;
    g_streamer.Stop();
    g_liveTargets.clear();
    g_streamStatus.clear();
    ShowMessage(message, false);
    if (!message.empty())
        Notify(Glyph::Live, Theme::SubText, message);
    UpdateAppIcon();
}

void ToggleStreaming()
{
    if (g_streamer.IsActive()) StopStreaming();
    else                       StartStreaming();
}

// حالة الوجهة رقم index إذا عم نبث عليها
const OutputStatus* TargetStatus(int index)
{
    for (size_t k = 0; k < g_liveTargets.size(); ++k)
        if (g_liveTargets[k] == index && k < g_streamStatus.size())
            return &g_streamStatus[k];
    return nullptr;
}

int LiveCount()
{
    int n = 0;
    for (const OutputStatus& st : g_streamStatus)
        if (st.state == OutputState::Live) ++n;
    return n;
}

// =============================================================
//  الإعادة: البرنامج بيحفظ آخر ثواني بالذاكرة
// =============================================================
void StartReplay(bool quiet = false)
{
    if (g_replay.IsActive())
        return;

    StreamOptions opt;
    opt.notify   = g_hwnd;
    opt.isReplay = true;
    opt.replay   = g_replayBuffer;
    g_replayBuffer->Clear();
    g_replayBuffer->SetSeconds(REPLAY_SECONDS[g_replaySet.seconds]);

    // ---- الصورة: الشاشة كاملة (الافتراضي) أو المشهد الحالي ----
    int cw = 0, ch = 0;
    g_replayMonitorHandle = nullptr;
    if (g_replaySet.source == 0)
    {
        const std::vector<CaptureTarget> mons = ReplayMonitors();
        if (!mons.empty())
        {
            const CaptureTarget& t = mons[std::clamp(g_replaySet.monitor, 0, (int)mons.size() - 1)];
            cw = (t.rect.right - t.rect.left) & ~1;
            ch = (t.rect.bottom - t.rect.top) & ~1;
            std::lock_guard<std::mutex> lock(g_replayScene.mutex);
            g_replayScene.canvasW = cw;
            g_replayScene.canvasH = ch;
            g_replayScene.layers.clear();
            SceneLayer l;
            l.id = 1;
            l.target = t;
            l.x = 0; l.y = 0; l.w = cw; l.h = ch;
            g_replayScene.layers.push_back(l);
            opt.scene = &g_replayScene;
            g_replayMonitorHandle = MonitorFromRect(&t.rect, MONITOR_DEFAULTTOPRIMARY);
        }
    }
    if (!opt.scene)                      // المشهد الحالي (أو ما لقينا شاشة)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        cw = g_scene.canvasW;
        ch = g_scene.canvasH;
        opt.scene = &g_scene;
    }

    // ---- الدقة والإطارات والجودة ----
    UINT32 h = g_replaySet.res == 1 ? 1080 : (g_replaySet.res == 2 ? 720 : (UINT32)ch);
    h = std::min<UINT32>(h, (UINT32)ch);
    UINT32 w = (UINT32)((double)cw * h / std::max(1, ch));
    opt.width     = std::max<UINT32>(16, w & ~1u);
    opt.height    = std::max<UINT32>(16, h & ~1u);
    opt.fps       = g_replaySet.fps == 0 ? 30 : 60;
    opt.videoKbps = ReplayKbps(g_replaySet, (int)opt.width, (int)opt.height);
    opt.encoder   = (EncoderMode)g_encoderMode;

    // ---- الصوت ----
    if (g_replaySet.system)
    {
        if (!g_systemAudio.IsRunning() && g_systemAudio.Start())
            g_systemForReplay = !g_systemOn;          // شغّلناه بس مشان الإعادة
        if (g_systemAudio.IsRunning())
            opt.audio.push_back(&g_systemAudio);
    }
    if (g_replaySet.mic && g_micOn && g_micAudio.IsRunning())
        opt.audio.push_back(&g_micAudio);
    if (g_replaySet.source == 1 && AnySceneHasApps())
        opt.audio.push_back(&g_appMixer.Output());

    std::wstring error;
    if (!g_replay.Start(opt, error))
    {
        ShowMessage(L"تعذّر تشغيل الإعادة: " + error, true);
        return;
    }
    g_replayWanted = true;
    SaveSettings();
    if (!quiet)
    {
        const std::wstring hint = KeyHint(HK_REPLAY_SAVE, L"لحفظ آخر " + std::to_wstring(REPLAY_SECONDS[g_replaySet.seconds]) + L" ثانية");
        ShowMessage(L"الإعادة تعمل" + (hint.empty() ? std::wstring() : L" — " + hint), false);
        Notify(Glyph::Replay, Theme::Accent, L"الإعادة تعمل", hint);
    }
}

void StopReplay()
{
    if (!g_replay.IsActive())
        return;
    g_replay.Stop();
    g_replayBuffer->Clear();
    g_replayWanted = false;
    if (g_systemForReplay && !g_systemOn)      // صوت النظام كان شغّال بس مشان الإعادة
        g_systemAudio.Stop();
    g_systemForReplay = false;
    SaveSettings();
    ShowMessage(L"أُوقفت الإعادة", false);
    Notify(Glyph::Replay, Theme::SubText, L"أُوقفت الإعادة");
}

void SaveReplay()
{
    if (!g_replay.IsActive())
    {
        ShowMessage(L"شغّل الإعادة أولاً (زر «الإعادة» في الأسفل)", true);
        return;
    }
    const std::wstring folder = ReplayFolder();
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, L"Replay_%04d-%02d-%02d_%02d-%02d-%02d.mp4",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring path = folder + L"\\" + name;

    std::wstring error;
    if (g_replayBuffer->Save(path, error))
    {
        g_lastFile = path;
        g_replaySavedTick = g_tick;          // المؤشر بيبيّن "حُفظ" ثانيتين
        ShowMessage(L"✓ حُفظت الإعادة: " + std::wstring(name), false);
        Notify(Glyph::Check, Theme::Green, L"حُفظت الإعادة", L"آخر " + std::to_wstring(REPLAY_SECONDS[g_replaySet.seconds]) + L" ثانية • " + name);
    }
    else
        ShowMessage(L"تعذّر حفظ الإعادة: " + error, true);
}

// إعدادات بتأثّر على الإعادة تغيّرت: منعيد تشغيلها
void RestartReplayIfActive()
{
    if (!g_replay.IsActive())
        return;
    g_replay.Stop();
    StartReplay(true);
}

// نافذة فلاتر المايك (نافذة داخلية، شوف MicFiltersPanel.cpp)
void ShowMicFilters()
{
    g_panels.Open(std::make_unique<MicFiltersPanel>(
        g_micAudio,
        [] { return g_micOn && g_micAudio.IsRunning(); },
        [] { SaveSettings(); }));
}

void OpenFolder()
{
    if (!g_lastFile.empty() && GetFileAttributesW(g_lastFile.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        const std::wstring args = L"/select,\"" + g_lastFile + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
    else
    {
        CreateDirectoryW(g_folder.c_str(), nullptr);
        ShellExecuteW(nullptr, L"open", g_folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

std::wstring FormatTime(LONGLONG ms)
{
    const LONGLONG s = ms / 1000;
    wchar_t buf[32];
    swprintf_s(buf, L"%02lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}

// =============================================================
//  رسم أجزاء الواجهة
// =============================================================

// ---------- الشريط العلوي ----------
void DrawTopBar(float W)
{
    Circle(W - 36, 34, 14, Theme::Red);
    Circle(W - 36, 34, 5, Theme::White);
    Text(L"مسجّل الشاشة", g_fTitle.Get(), Rect(W - 330, 10, W - 60, 58), Theme::Text);

    // زر الإعدادات (ترس) جنب العنوان
    {
        const float tw = TextWidth(L"مسجّل الشاشة", g_fTitle.Get());
        const D2D1_RECT_F gear = Rect(W - 60 - tw - 140, 16, W - 60 - tw - 16, 52);
        const bool hover = AddHit(gear, ID_OPEN_SETTINGS);
        Fill(gear, hover ? Theme::Hover : Theme::Panel, 10);
        Stroke(gear, Theme::Border, 10);
        Text(Icon::Settings, g_fIcon.Get(), Rect(gear.right - 34, gear.top, gear.right - 10, gear.bottom), hover ? Theme::Text : Theme::SubText);
        Text(L"الإعدادات", g_fSegment.Get(), Rect(gear.left + 8, gear.top, gear.right - 36, gear.bottom), hover ? Theme::Text : Theme::SubText);
    }

    const bool rec    = g_recorder.IsRecording();
    const bool paused = g_recorder.IsPaused();
    D2D1_COLOR_F color = Theme::SubText;
    const wchar_t* label = L"جاهز";
    if (rec && paused) { color = Theme::Yellow; label = L"متوقف مؤقتاً"; }
    else if (rec)      { color = Theme::Red;    label = L"جارٍ التسجيل"; }

    const D2D1_RECT_F pill = Rect(16, 18, 146, 50);
    Fill(pill, WithAlpha(color, 0.15f), 16);
    if (!rec || paused || (g_tick / 15) % 2 == 0)
        Circle(pill.right - 18, 34, 5, color);
    Text(label, g_fCenter.Get(), Rect(pill.left + 6, pill.top, pill.right - 26, pill.bottom), color);

    Text(FormatTime(g_recorder.ElapsedMs()), g_fTimer.Get(), Rect(156, 10, 300, 58),
         rec ? Theme::Text : Theme::SubText);

    // شارة البث المباشر: "مباشر 00:12:34"
    if (g_streamer.IsActive())
    {
        const int  live  = LiveCount();
        const D2D1_COLOR_F c = live > 0 ? D2D1::ColorF(0xFF3B5C) : Theme::Yellow;
        const D2D1_RECT_F lp = Rect(312, 18, 492, 50);
        Fill(lp, WithAlpha(c, 0.16f), 16);
        if (live == 0 || (g_tick / 15) % 2 == 0)
            Circle(lp.right - 18, 34, 5, c);
        Text(live > 0 ? L"مباشر" : L"اتصال…", g_fSegment.Get(), Rect(lp.right - 80, lp.top, lp.right - 26, lp.bottom), c);
        Text(FormatTime(g_streamer.ElapsedMs()), g_fSmallLtr.Get(), Rect(lp.left + 8, lp.top, lp.right - 84, lp.bottom), Theme::Text);
    }
}

// مستطيل الطبقة بإحداثيات الشاشة (DIP)
D2D1_RECT_F LayerToDip(const SceneLayer& l, int cw)
{
    const float s = (g_previewDst.right - g_previewDst.left) / cw;
    return Rect(g_previewDst.left + (float)l.x * s, g_previewDst.top + (float)l.y * s,
                g_previewDst.left + (float)(l.x + l.w) * s, g_previewDst.top + (float)(l.y + l.h) * s);
}

// مكان المقابض الثمانية: 0 فوق-يسار، 1 فوق، 2 فوق-يمين، 3 يمين، 4 تحت-يمين، 5 تحت، 6 تحت-يسار، 7 يسار
D2D1_POINT_2F HandlePos(const D2D1_RECT_F& r, int i)
{
    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    const D2D1_POINT_2F p[8] = { {r.left, r.top}, {cx, r.top}, {r.right, r.top}, {r.right, cy},
                                 {r.right, r.bottom}, {cx, r.bottom}, {r.left, r.bottom}, {r.left, cy} };
    return p[i];
}

// ---------- المعاينة (اللوحة) ----------
void DrawPreview(const D2D1_RECT_F& area)
{
    const bool rec = g_recorder.IsRecording();
    Fill(area, Theme::Panel, 14);
    if (rec)
        Stroke(area, g_recorder.IsPaused() ? Theme::Yellow : Theme::Red, 14, 2);

    // ---- شريط المشاهد (فوق المعاينة) ----
    const float sy = area.top + 12, sh = 34;
    const D2D1_RECT_F strip = Rect(area.left + 12, sy, area.right - 12, sy + sh);
    g_rt->PushAxisAlignedClip(strip, D2D1_ANTIALIAS_MODE_ALIASED);
    float sx = strip.right;
    for (int i = 0; i < (int)g_scenes.size(); ++i)
    {
        const bool active = (i == g_activeScene);
        const float w = std::min(220.0f, TextWidth(g_scenes[i].name, g_fSegment.Get()) + 32);
        const D2D1_RECT_F pill = Rect(sx - w, sy, sx, sy + sh);
        const bool hover = AddHit(pill, ID_SCENE_FIRST + i);
        Fill(pill, active ? Theme::Accent : (hover ? Theme::Hover : Theme::Card), 9);
        Text(g_scenes[i].name, g_fSegment.Get(), Rect(pill.left + 12, pill.top, pill.right - 12, pill.bottom),
             active ? Theme::White : Theme::Text);
        if (active && rec)          // نقطة حمرا: هاد المشهد عم يتسجّل
            Circle(pill.right - 7, pill.top + 7, 3.5f, g_recorder.IsPaused() ? Theme::Yellow : Theme::Red);
        sx -= w + 6;
    }
    const D2D1_RECT_F addScene = Rect(sx - 34, sy, sx, sy + sh);
    Fill(addScene, AddHit(addScene, ID_ADD_SCENE) ? Theme::Hover : Theme::Card, 9);
    Text(Icon::Add, g_fIcon.Get(), addScene, Theme::SubText);
    g_rt->PopAxisAlignedClip();

    const D2D1_RECT_F inner = Rect(area.left + 14, sy + sh + 12, area.right - 14, area.bottom - 46);
    g_previewArea = inner;

    int cw, ch;
    std::vector<SceneLayer> layers;
    g_scene.Snapshot(cw, ch, layers);

    // اللوحة بنسبة الفيديو، بالنص
    const float iw = inner.right - inner.left, ih = inner.bottom - inner.top;
    const float fit = std::min(iw / cw, ih / ch);
    const float dw = cw * fit, dh = ch * fit;
    g_previewDst = Rect(inner.left + (iw - dw) / 2, inner.top + (ih - dh) / 2,
                        inner.left + (iw + dw) / 2, inner.top + (ih + dh) / 2);

    if (g_previewValid && g_previewBitmap)
        g_rt->DrawBitmap(g_previewBitmap.Get(), g_previewDst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else
        Fill(g_previewDst, D2D1::ColorF(0x000000));
    Stroke(g_previewDst, Theme::Border, 0, 1);

    if (layers.empty())
        Text(L"المشهد فارغ — أضف مصدرًا من القائمة", g_fCenter.Get(), g_previewDst, Theme::SubText);

    // إطار الطبقة المختارة ومقابضها
    g_rt->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_ALIASED);
    for (const SceneLayer& l : layers)
    {
        const D2D1_RECT_F r = LayerToDip(l, cw);
        if (l.id == g_selectedLayer)
        {
            // وضع القص (Alt): إطار أخضر، وإطار خفيف للمصدر الكامل لتعرف شو مقصوص
            const bool cropping = (GetKeyState(VK_MENU) & 0x8000) != 0 && (g_drag == Drag::None || g_drag == Drag::Resize);
            const D2D1_COLOR_F c = cropping ? Theme::Green : Theme::Accent;
            if (cropping && l.HasCrop())
            {
                const float fw = (r.right - r.left) / (float)std::max(0.05, 1 - l.cropL - l.cropR);
                const float fh = (r.bottom - r.top) / (float)std::max(0.05, 1 - l.cropT - l.cropB);
                const float fx = r.left - (float)l.cropL * fw, fy = r.top - (float)l.cropT * fh;
                const D2D1_RECT_F full = Rect(fx, fy, fx + fw, fy + fh);
                Fill(Rect(full.left, full.top, full.right, r.top), D2D1::ColorF(0x000000, 0.45f));        // المقصوص فوق
                Fill(Rect(full.left, r.bottom, full.right, full.bottom), D2D1::ColorF(0x000000, 0.45f));  // تحت
                Fill(Rect(full.left, r.top, r.left, r.bottom), D2D1::ColorF(0x000000, 0.45f));            // يسار
                Fill(Rect(r.right, r.top, full.right, r.bottom), D2D1::ColorF(0x000000, 0.45f));          // يمين
                Stroke(full, WithAlpha(Theme::White, 0.35f), 0, 1);
            }
            Stroke(r, c, 0, 2);
            for (int i = 0; i < 8; ++i)
            {
                const D2D1_POINT_2F p = HandlePos(r, i);
                Fill(Rect(p.x - 5, p.y - 5, p.x + 5, p.y + 5), cropping ? c : Theme::White);
                Stroke(Rect(p.x - 5, p.y - 5, p.x + 5, p.y + 5), cropping ? Theme::White : c, 0, 1);
            }
            if (cropping)
                Text(L"قص", g_fSmallLtr.Get(), Rect(r.left, r.top - 22, r.left + 40, r.top - 4), Theme::Green);
        }
        else if (Inside(r, g_mouse) && g_drag == Drag::None)
        {
            Stroke(r, WithAlpha(Theme::Accent, 0.5f), 0, 1);
        }
        else if (g_drag != Drag::None)       // وقت السحب: إطار خفيف لكل المصادر التانية
        {
            Stroke(r, WithAlpha(Theme::White, 0.25f), 0, 1);
        }
    }

    // خطوط المغناطيس (وردية متل برامج التصميم)
    if (g_drag != Drag::None)
    {
        const float s = (g_previewDst.right - g_previewDst.left) / cw;
        g_brush->SetColor(D2D1::ColorF(0xFF4FA3));
        for (double gx : g_guidesX)
        {
            const float x = g_previewDst.left + (float)gx * s;
            g_rt->DrawLine(D2D1::Point2F(x, g_previewDst.top), D2D1::Point2F(x, g_previewDst.bottom), g_brush.Get(), 1.5f);
        }
        for (double gy : g_guidesY)
        {
            const float y = g_previewDst.top + (float)gy * s;
            g_rt->DrawLine(D2D1::Point2F(g_previewDst.left, y), D2D1::Point2F(g_previewDst.right, y), g_brush.Get(), 1.5f);
        }
    }
    g_rt->PopAxisAlignedClip();

    // تحت: الدقة • الإطارات • معدّل البت
    const std::wstring summary = L"اللوحة: " + SizeText(cw, ch)
                               + L"  •  " + std::to_wstring(CurrentFps()) + L" إطار/ث"
                               + L"  •  " + MbpsText(CurrentBitrate()) + L" ميغابت/ث";
    Text(summary, g_fSmall.Get(), Rect(area.left + 16, area.bottom - 42, area.right - 16, area.bottom - 14),
         rec ? Theme::Text : Theme::SubText);
    Text(L"اسحب للتحريك • المقابض لتغيير الحجم • Alt مع المقبض للقص • Ctrl بدون التصاق • الزر الأيمن للمزيد", g_fSmallEnd.Get(),
         Rect(area.left + 16, area.bottom - 42, area.right - 16, area.bottom - 14), Theme::Off);
}

// ---------- سطر مصدر صوت ----------
void DrawAudioRow(const D2D1_RECT_F& r, const wchar_t* icon, const wchar_t* title, AudioSource& src,
                  bool on, float level, int toggleId, int deviceId, int muteId, int volId, int index)
{
    const bool locked = Busy();
    const bool muted  = src.IsMuted();
    Fill(r, Theme::Card, 10);

    // الأيقونة = زر الكتم (بيشتغل حتى وقت التسجيل)
    const D2D1_RECT_F mute = Rect(r.right - 46, r.top + 6, r.right - 8, r.top + 40);
    if (AddHit(mute, muteId))
        Fill(mute, Theme::Hover, 8);
    Text(muted ? Icon::Muted : icon, g_fIconBig.Get(), mute,
         muted ? Theme::Red : (on ? Theme::Text : Theme::SubText));
    // المايك: زر الفلاتر جنب زر الجهاز
    const float textLeft = index == 1 ? r.left + 130 : r.left + 96;
    if (index == 1)
    {
        const bool anyFilter = src.Filters().Any();
        const D2D1_RECT_F fb = Rect(r.left + 94, r.top + 9, r.left + 126, r.top + 41);
        if (AddHit(fb, ID_MIC_FILTERS))
            Fill(fb, Theme::Hover, 8);
        else if (anyFilter)
            Fill(fb, WithAlpha(Theme::Accent, 0.16f), 8);
        Text(Icon::Filter, g_fIcon.Get(), fb, anyFilter ? Theme::Accent : Theme::SubText);
    }
    Text(title, g_fBody.Get(), Rect(textLeft, r.top + 6, r.right - 50, r.top + 26), Theme::Text);

    std::wstring sub;
    D2D1_COLOR_F subColor = Theme::SubText;
    if (!on)                    sub = L"معطّل";
    else if (!src.IsRunning()) { sub = L"لم يُعثر على جهاز"; subColor = Theme::Red; }
    else if (muted)            { sub = L"مكتوم"; subColor = Theme::Red; }
    else                        sub = src.DeviceName();
    Text(sub, g_fSmall.Get(), Rect(textLeft, r.top + 26, r.right - 50, r.top + 44), subColor);

    // مفتاح التشغيل
    const D2D1_RECT_F toggle = Rect(r.left + 10, r.top + 12, r.left + 54, r.top + 38);
    AddHit(toggle, toggleId);
    DrawToggle(r.left + 12, r.top + 13, on, !locked);

    // زر اختيار الجهاز
    const D2D1_RECT_F dev = Rect(r.left + 58, r.top + 9, r.left + 90, r.top + 41);
    if (AddHit(dev, deviceId) && !locked)
        Fill(dev, Theme::Hover, 8);
    Text(Icon::Chevron, g_fIcon.Get(), dev, locked ? Theme::Off : Theme::SubText);

    // ---- شريط العلو + مؤشر الصوت جوّاته ----
    // من اليمين لليسار: 0% على اليمين و200% على اليسار
    const float cy = r.bottom - 20;
    const D2D1_RECT_F track = Rect(r.left + 58, cy - 3, r.right - 14, cy + 3);
    const float tw = track.right - track.left;
    g_volTrack[index] = track;
    AddHit(Rect(track.left - 8, cy - 12, track.right + 8, cy + 12), volId);

    const bool active = on && src.IsRunning() && !muted;
    Fill(track, Theme::Border, 3);
    if (active && level > 0)       // مستوى الصوت الحالي (بعد العلو)
    {
        const D2D1_COLOR_F c = level < 0.7f ? Theme::Green : (level < 0.9f ? Theme::Yellow : Theme::Red);
        Fill(Rect(track.right - tw * level, track.top, track.right, track.bottom), WithAlpha(c, 0.85f), 3);
    }

    // المقبض
    const float vol = src.Volume();
    const float kx = track.right - tw * (vol / 2.0f);
    const bool  dragging = (g_volDrag == &src);
    Circle(kx, cy, dragging ? 8.0f : 7.0f, muted ? Theme::Off : Theme::White);
    Circle(kx, cy, 3.0f, muted ? Theme::SubText : Theme::Accent);

    // النسبة
    Text(std::to_wstring((int)std::lround(vol * 100)) + L"%", g_fSmallLtr.Get(),
         Rect(r.left + 8, cy - 10, r.left + 54, cy + 10), muted ? Theme::Off : (dragging ? Theme::Accent : Theme::SubText));
}

// من مكان الماوس لعلو الصوت (مع مغناطيس على 100%)
void SetVolumeFromX(AudioSource& src, const D2D1_RECT_F& track, float x)
{
    float f = (track.right - x) / (track.right - track.left);
    f = std::clamp(f, 0.0f, 1.0f);
    float vol = f * 2.0f;
    if (std::fabs(vol - 1.0f) < 0.05f)
        vol = 1.0f;
    src.SetVolume(vol);
}

// من مكان الماوس لعلو (0..2، مع مغناطيس على 100%)
float VolumeAtX(const D2D1_RECT_F& track, float x)
{
    const float f = std::clamp((track.right - x) / (track.right - track.left), 0.0f, 1.0f);
    const float vol = f * 2.0f;
    return std::fabs(vol - 1.0f) < 0.05f ? 1.0f : vol;
}

// ---------- سطر صوت برنامج (بقائمة المصادر) ----------
void DrawAppAudioRow(const D2D1_RECT_F& row, int i)
{
    const AppAudioEntry e = g_appMixer.Entry(i);
    const bool connected  = g_appMixer.IsConnected(i);

    const D2D1_RECT_F del  = Rect(row.left + 6, row.top + 8, row.left + 36, row.top + 38);
    const D2D1_RECT_F mute = Rect(row.left + 40, row.top + 8, row.left + 70, row.top + 38);
    const bool hover     = AddHit(row, ID_APP_ROW_FIRST + i);
    const bool delHover  = AddHit(del, ID_APP_DELETE_FIRST + i);
    const bool muteHover = AddHit(mute, ID_APP_MUTE_FIRST + i);

    Fill(row, hover ? Theme::Hover : Theme::Card, 10);
    Text(Icon::Speaker, g_fIconBig.Get(), Rect(row.right - 44, row.top + 2, row.right - 10, row.top + 40),
         connected ? Theme::Accent : Theme::SubText);
    Text(e.name, g_fBody.Get(), Rect(row.left + 76, row.top + 4, row.right - 50, row.top + 24),
         connected ? Theme::Text : Theme::SubText);

    std::wstring sub = L"صوت برنامج";
    D2D1_COLOR_F subColor = Theme::SubText;
    if (!connected)   { sub = L"البرنامج غير مفتوح"; subColor = Theme::Yellow; }
    else if (e.muted) { sub = L"مكتوم"; subColor = Theme::Red; }
    Text(sub, g_fSmall.Get(), Rect(row.left + 76, row.top + 23, row.right - 50, row.top + 41), subColor);

    if (delHover)  Fill(del, D2D1::ColorF(0xE5484D, 0.2f), 6);
    if (muteHover) Fill(mute, Theme::Hover, 6);
    Text(Icon::Delete, g_fIcon.Get(), del, delHover ? Theme::Red : Theme::SubText);
    Text(e.muted ? Icon::Muted : Icon::Speaker, g_fIcon.Get(), mute, e.muted ? Theme::Red : Theme::Text);

    // شريط العلو + مستوى الصوت جوّاته (من اليمين لليسار متل باقي الأشرطة)
    const float cy = row.bottom - 13;
    const D2D1_RECT_F track = Rect(row.left + 58, cy - 3, row.right - 14, cy + 3);
    const float tw = track.right - track.left;
    if (i < (int)g_appTracks.size())
        g_appTracks[i] = track;
    AddHit(Rect(track.left - 8, cy - 10, track.right + 8, cy + 10), ID_APP_VOL_FIRST + i);

    Fill(track, Theme::Border, 3);
    const float level = i < (int)g_appLevels.size() ? g_appLevels[i] : 0.0f;
    if (connected && !e.muted && level > 0)
    {
        const D2D1_COLOR_F c = level < 0.7f ? Theme::Green : (level < 0.9f ? Theme::Yellow : Theme::Red);
        Fill(Rect(track.right - tw * level, track.top, track.right, track.bottom), WithAlpha(c, 0.85f), 3);
    }
    const bool  dragging = (g_appVolDrag == i);
    const float kx = track.right - tw * (e.volume / 2.0f);
    Circle(kx, cy, dragging ? 8.0f : 7.0f, e.muted ? Theme::Off : Theme::White);
    Circle(kx, cy, 3.0f, e.muted ? Theme::SubText : Theme::Accent);
    Text(std::to_wstring((int)std::lround(e.volume * 100)) + L"%", g_fSmallLtr.Get(),
         Rect(row.left + 8, cy - 10, row.left + 54, cy + 10), e.muted ? Theme::Off : (dragging ? Theme::Accent : Theme::SubText));
}

// ---------- تبويب المصادر: طبقات المشهد + الصوت ----------
void DrawSourcesTab(const D2D1_RECT_F& side, float y)
{
    // العنوان + زر الإضافة
    const std::wstring sceneName = (g_activeScene < (int)g_scenes.size()) ? g_scenes[g_activeScene].name : L"";
    Text(L"طبقات: " + sceneName, g_fSection.Get(), Rect(side.left + 140, y, side.right - 18, y + 30), Theme::SubText);
    const D2D1_RECT_F add = Rect(side.left + 12, y, side.left + 132, y + 30);
    Fill(add, AddHit(add, ID_ADD_SOURCE) ? D2D1::ColorF(0x6A9DFF) : Theme::Accent, 8);
    Text(L"+  إضافة مصدر", g_fSegment.Get(), add, Theme::White);

    const float audioHeight = 226;
    g_listRect = Rect(side.left + 8, y + 38, side.right - 8, side.bottom - audioHeight);

    std::vector<SceneLayer> layers;
    int cw, ch;
    g_scene.Snapshot(cw, ch, layers);

    const float rowH = 50, gap = 4;
    // أصوات البرامج بتنعرض بعد الطبقات: عنوان + سطر لكل برنامج (+ تنبيه إذا صوت النظام شغّال)
    const int   appCount = (int)g_appMixer.Count();
    const float appRowH  = 64;
    const bool  appWarn  = appCount > 0 && g_systemOn;
    const float appsH    = appCount == 0 ? 0 : 32 + appCount * (appRowH + gap) + (appWarn ? 40 : 0);
    const float contentH = layers.size() * (rowH + gap) + appsH;
    const float listH    = g_listRect.bottom - g_listRect.top;
    g_scrollMax = std::max(0.0f, contentH - listH);
    g_scroll = std::clamp(g_scroll, 0.0f, g_scrollMax);

    if (layers.empty() && appCount == 0)
        Text(L"لا توجد مصادر بعد", g_fCenter.Get(), g_listRect, Theme::SubText);

    g_rt->PushAxisAlignedClip(g_listRect, D2D1_ANTIALIAS_MODE_ALIASED);
    // الطبقة اللي فوق الكل منعرضها أول شي
    for (int k = 0; k < (int)layers.size(); ++k)
    {
        const SceneLayer& l = layers[layers.size() - 1 - k];
        const float top = g_listRect.top + k * (rowH + gap) - g_scroll;
        if (top + rowH < g_listRect.top || top > g_listRect.bottom)
            continue;
        const D2D1_RECT_F row = Rect(g_listRect.left, top, g_listRect.right, top + rowH);
        const D2D1_RECT_F vis = Rect(row.left, std::max(row.top, g_listRect.top), row.right, std::min(row.bottom, g_listRect.bottom));
        const bool selected = (l.id == g_selectedLayer);

        // زرّين على اليسار: حذف + إظهار/إخفاء (منسجّلهم قبل السطر لحتى ياخدوا الأولوية)
        const D2D1_RECT_F del = Rect(row.left + 6, row.top + 10, row.left + 36, row.top + 40);
        const D2D1_RECT_F eye = Rect(row.left + 40, row.top + 10, row.left + 70, row.top + 40);
        const bool hover = AddHit(vis, ID_LAYER_FIRST + l.id);
        const bool delHover = AddHit(del, ID_DELETE_FIRST + l.id);
        const bool eyeHover = AddHit(eye, ID_EYE_FIRST + l.id);

        if (selected)
        {
            Fill(row, WithAlpha(Theme::Accent, 0.16f), 10);
            Stroke(row, WithAlpha(Theme::Accent, 0.7f), 10, 1);
        }
        else if (hover)
        {
            Fill(row, Theme::Hover, 10);
        }

        const D2D1_COLOR_F textColor = l.visible ? Theme::Text : Theme::SubText;
        const wchar_t* kindIcon = l.kind == LayerKind::Camera ? Icon::Camera : l.kind == LayerKind::Image ? Icon::Photo
                                : l.kind == LayerKind::Text ? Icon::Font : (l.target.isMonitor ? Icon::Monitor : Icon::Window);
        Text(kindIcon, g_fIconBig.Get(),
             Rect(row.right - 44, row.top, row.right - 10, row.bottom), selected ? Theme::Accent : Theme::SubText);
        Text(l.target.name, g_fBody.Get(), Rect(row.left + 76, row.top + 5, row.right - 50, row.top + 27), textColor);

        std::wstring sub = l.kind == LayerKind::Camera ? L"كاميرا" : l.kind == LayerKind::Image ? L"صورة"
                         : l.kind == LayerKind::Text ? L"نص" : (l.target.isMonitor ? L"شاشة كاملة" : L"نافذة");
        D2D1_COLOR_F subColor = Theme::SubText;
        if (l.lost)
        {
            sub = l.kind == LayerKind::Camera ? L"الكاميرا غير متصلة" : l.kind == LayerKind::Image ? L"الملف غير موجود" : L"النافذة مغلقة";
            subColor = Theme::Red;
        }
        else if (!l.visible) { sub += L" • مخفية"; }
        Text(sub, g_fSmall.Get(), Rect(row.left + 76, row.top + 26, row.right - 50, row.top + 44), subColor);

        if (delHover) Fill(del, D2D1::ColorF(0xE5484D, 0.2f), 6);
        if (eyeHover) Fill(eye, Theme::Hover, 6);
        Text(Icon::Delete, g_fIcon.Get(), del, delHover ? Theme::Red : Theme::SubText);
        Text(Icon::Eye, g_fIcon.Get(), eye, l.visible ? Theme::Text : Theme::Off);
    }

    // ---- أصوات البرامج ----
    g_appTracks.assign(appCount, D2D1_RECT_F{ -1, -1, -1, -1 });
    if (appCount > 0)
    {
        g_hitClipOn = true;               // اللي برّا القائمة (مخفي بالتمرير) ما بينكبس
        g_hitClip   = g_listRect;
        float ay = g_listRect.top + layers.size() * (rowH + gap) - g_scroll;
        Text(L"أصوات البرامج", g_fSection.Get(), Rect(g_listRect.left + 10, ay + 6, g_listRect.right - 10, ay + 28), Theme::SubText);
        ay += 32;
        for (int i = 0; i < appCount; ++i)
        {
            if (ay + appRowH >= g_listRect.top && ay <= g_listRect.bottom)
                DrawAppAudioRow(Rect(g_listRect.left, ay, g_listRect.right, ay + appRowH), i);
            ay += appRowH + gap;
        }
        if (appWarn)
        {
            Text(L"«صوت النظام» يلتقط هذه البرامج أيضًا،", g_fSmall.Get(),
                 Rect(g_listRect.left + 10, ay + 2, g_listRect.right - 10, ay + 20), Theme::Yellow);
            Text(L"فأطفئه لتجنّب تكرار الصوت", g_fSmall.Get(),
                 Rect(g_listRect.left + 10, ay + 20, g_listRect.right - 10, ay + 38), Theme::Yellow);
        }
        g_hitClipOn = false;
    }
    g_rt->PopAxisAlignedClip();

    // ---- قسم الصوت ----
    const float ay = side.bottom - audioHeight;
    Fill(Rect(side.left + 16, ay + 4, side.right - 16, ay + 5), Theme::Border);
    Text(L"الصوت", g_fSection.Get(), Rect(side.left + 18, ay + 10, side.right - 18, ay + 36), Theme::SubText);
    DrawAudioRow(Rect(side.left + 8, ay + 40, side.right - 8, ay + 124), Icon::Speaker, L"صوت النظام",
                 g_systemAudio, g_systemOn, g_systemLevel, ID_SYSTEM_AUDIO, ID_SYSTEM_DEVICE, ID_SYSTEM_MUTE, ID_SYSTEM_VOL, 0);
    DrawAudioRow(Rect(side.left + 8, ay + 130, side.right - 8, ay + 214), Icon::Mic, L"الميكروفون",
                 g_micAudio, g_micOn, g_micLevel, ID_MIC, ID_MIC_DEVICE, ID_MIC_MUTE, ID_MIC_VOL, 1);
}

// ---------- صفحات نافذة الإعدادات (الفيديو، التسجيل، المؤشر، عام) ----------
// الأرقام + SETTINGS_ID_OFFSET لحتى توصل الكبسة للنافذة، وهي بترجّعها لـ OnClick
float DrawSettingsPage(int page, float L, float top, float R)
{
    const int   O = SETTINGS_ID_OFFSET;
    const bool  locked = Busy();
    float y = top;

    auto Label = [&](const std::wstring& s) { Text(s, g_fSection.Get(), Rect(L + 16, y, R - 16, y + 22), Theme::Text); y += 26; };
    auto Hint  = [&](const std::wstring& s, D2D1_COLOR_F c = Theme::SubText)
    {
        Text(s, g_fSmall.Get(), Rect(L + 16, y, R - 16, y + 18), c);
        y += 22;
    };
    const float l = L + 16, r = R - 16;
    auto Locked = [&]()
    {
        if (!locked)
            return;
        Fill(Rect(L, y, R, y + 40), WithAlpha(Theme::Yellow, 0.12f), 10);
        Text(L"لا يمكن تغيير هذه الإعدادات أثناء التسجيل أو البث", g_fBody.Get(), Rect(L + 16, y, R - 16, y + 40), Theme::Yellow);
        y += 50;
    };

    // كل خيار بكرت لحالو: عنوان، أزرار، شرح
    auto Option = [&](const std::wstring& title, auto&& controls)
    {
        const float start = y;
        y += 12;
        Label(title);
        controls();
        y += 6;
        // إطار حول الخيار
        Stroke(Rect(L + 0.5f, start + 0.5f, R - 0.5f, y - 0.5f), Theme::Border, 12);
        y += 10;
    };

    switch (page)
    {
    case SP_VIDEO:
    {
        Locked();
        int pw, ph;
        PrimaryMonitorSize(pw, ph);
        Option(L"دقة الفيديو", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"الشاشة", L"1440p", L"1080p", L"720p" }, g_canvasChoice, O + ID_CANVAS_FIRST, !locked);
            y += 40;
            Hint(g_canvasChoice == 0 ? L"بحجم الشاشة الرئيسية ← " + SizeText(pw & ~1, ph & ~1)
                                     : SizeText(CANVAS_H[g_canvasChoice] * 16 / 9, CANVAS_H[g_canvasChoice]));
        });
        Option(L"معدّل الإطارات", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"تلقائي", L"24", L"30", L"60" }, g_fpsChoice, O + ID_FPS_FIRST, !locked);
            y += 40;
            Hint(g_fpsChoice == 0 ? L"تلقائي: حسب تردد الشاشة ← " + std::to_wstring(CurrentFps()) + L" إطار/ث"
                                  : std::wstring(L"عدد ثابت من الإطارات في الثانية"));
        });
        Option(L"الجودة ومعدّل البت", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"منخفضة", L"متوسطة", L"عالية", L"مخصّصة" }, g_quality, O + ID_QUALITY_FIRST, !locked);
            y += 42;
            const D2D1_RECT_F box   = Rect(l, y, r, y + 34);
            const D2D1_RECT_F plus  = Rect(r - 40, y + 3, r - 3, y + 31);
            const D2D1_RECT_F minus = Rect(l + 3, y + 3, l + 40, y + 31);
            Fill(box, Theme::Background, 9);
            if (AddHit(plus, O + ID_BITRATE_PLUS) && !locked)   Fill(plus, Theme::Hover, 7);
            if (AddHit(minus, O + ID_BITRATE_MINUS) && !locked) Fill(minus, Theme::Hover, 7);
            Text(L"+", g_fCenter.Get(), plus,  locked ? Theme::Off : Theme::Text);
            Text(L"−", g_fCenter.Get(), minus, locked ? Theme::Off : Theme::Text);
            Text(MbpsText(CurrentBitrate()) + L" ميغابت/ث", g_fCenter.Get(), Rect(l + 44, y, r - 44, y + 34), Theme::Text);
            y += 40;
            Hint(L"الأعلى = صورة أوضح وملف أكبر");
        });
        Option(L"المشفّر", [&]
        {
            const D2D1_RECT_F enc = Rect(l, y, r, y + 34);
            Fill(enc, AddHit(enc, O + ID_ENCODER_MENU) && !locked ? Theme::Hover : Theme::Background, 9);
            Text(EncoderLabel(), g_fBody.Get(), Rect(l + 34, y, r - 12, y + 34), locked ? Theme::SubText : Theme::Text);
            Text(Icon::Chevron, g_fIcon.Get(), Rect(l + 4, y, l + 30, y + 34), Theme::SubText);
            y += 40;
            const bool gpu = g_previewCompositor.UsingGpu();
            Hint(gpu ? L"محرّك التصوير: بطاقة الرسومات (Windows Graphics Capture)"
                     : L"محرّك التصوير: الطريقة القديمة (GDI) — بطاقة الرسومات غير متاحة",
                 gpu ? Theme::Green : Theme::Yellow);
        });
        break;
    }

    case SP_RECORDING:
    {
        Locked();
        Option(L"مكان حفظ التسجيلات", [&]
        {
            const D2D1_RECT_F box = Rect(l, y, r, y + 34);
            Fill(box, Theme::Background, 9);
            Text(LtrText(g_folder), g_fSmallEnd.Get(), Rect(l + 10, y, r - 10, y + 34), Theme::Text);
            y += 42;
            DrawButton(Rect(r - 130, y, r, y + 32), L"تغيير…", O + ID_FOLDER_CHANGE, false, !locked, Icon::Folder);
            DrawButton(Rect(r - 270, y, r - 140, y + 32), L"فتح المجلد", O + ID_FOLDER_OPEN, false, true);
            y += 40;
        });
        Option(L"صيغة الملف", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"MP4 آمن", L"MP4 عادي" }, g_crashSafe ? 0 : 1, O + ID_CONTAINER_FIRST, !locked);
            y += 40;
            Hint(g_crashSafe ? std::wstring(L"لا يضيع التسجيل إذا تعطّل البرنامج أو انقطعت الكهرباء")
                             : std::wstring(L"يضيع التسجيل كله إذا تعطّل البرنامج قبل الإيقاف"));
        });
        Option(L"مسارات الصوت", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"مسار واحد", L"مسارات منفصلة" }, g_separateTracks ? 1 : 0, O + ID_AUDIO_TRACKS_FIRST, !locked);
            y += 40;
            Hint(g_separateTracks ? std::wstring(L"المسار 1: الكل، ثم النظام، الميكروفون، البرامج — مفيد للمونتاج")
                                  : std::wstring(L"كل الأصوات مدمجة في مسار واحد"));
        });
        break;
    }

    case SP_OVERLAY:
    {
        Option(L"مؤشر التسجيل", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"مخفي", L"◥", L"◤", L"◢", L"◣" },
                          g_overlayCorner, O + ID_OVERLAY_FIRST, true);
            y += 40;
            static const wchar_t* where[] = { L"لا يظهر أي مؤشر", L"أعلى يمين الشاشة", L"أعلى يسار الشاشة",
                                              L"أسفل يمين الشاشة", L"أسفل يسار الشاشة" };
            Hint(std::wstring(where[g_overlayCorner]) + (g_overlayCorner ? L" • الوقت، المايك، الإعادة" : L""));
        });
        Option(L"الإشعارات", [&]
        {
            DrawSegmented(Rect(l + 140, y, r, y + 34), { L"تظهر", L"لا تظهر" }, g_toastsOn ? 0 : 1, O + ID_TOASTS_FIRST, true);
            DrawButton(Rect(l, y, l + 130, y + 34), L"تجربة", O + ID_TEST_TOAST, false, g_toastsOn);
            y += 40;
            Hint(L"بدء التسجيل وحفظه، الإعادة، البث، المايك... في زاوية المؤشر");
        });
        Option(L"عندما تكون نافذة البرنامج أمامك", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"تظهر دائمًا", L"تختفي" }, g_overlayOverApp ? 0 : 1, O + ID_OVERAPP_FIRST, true);
            y += 40;
            Hint(g_overlayOverApp ? std::wstring(L"تظهر في كل الحالات: سطح المكتب، الألعاب، وفوق البرنامج")
                                  : std::wstring(L"تظهر فقط عندما يكون البرنامج مصغّرًا أو خلف نافذة أخرى"));
        });
        Hint(L"المؤشر والإشعارات لا يظهران في التسجيل ولا في البث", Theme::Green);
        break;
    }

    case SP_GENERAL:
    {
        Option(L"عند الضغط على ✕", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"بجانب الساعة", L"إغلاق البرنامج" }, g_closeToTray ? 0 : 1, O + ID_CLOSE_FIRST, true);
            y += 40;
            Hint(g_closeToTray ? std::wstring(L"يبقى البرنامج يعمل، والخروج من أيقونته بجانب الساعة")
                               : std::wstring(L"يُغلق البرنامج تمامًا"));
        });
        Option(L"التشغيل مع بدء ويندوز", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"لا", L"نعم، بجانب الساعة" }, IsRunAtStartup() ? 1 : 0, O + ID_STARTUP_FIRST, true);
            y += 40;
            Hint(L"يبدأ مخفيًّا بجانب الساعة، جاهزًا للاختصارات والإعادة");
        });
        Option(L"الانتقال بين المشاهد", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"فوري", L"تلاشٍ سريع", L"تلاشٍ ناعم" }, g_transitionChoice, O + ID_TRANSITION_FIRST, true);
            y += 40;
        });
        break;
    }
    }
    return y - top;
}

// ---------- تبويب البث: الوجهات + إعدادات البث ----------
void DrawStreamTab(const D2D1_RECT_F& side, float y)
{
    const bool live = g_streamer.IsActive();
    const float L = side.left + 16, R = side.right - 16;

    Text(L"وجهات البث", g_fSection.Get(), Rect(side.left + 140, y, side.right - 18, y + 30), Theme::SubText);
    const D2D1_RECT_F add = Rect(side.left + 12, y, side.left + 132, y + 30);
    Fill(add, live ? Theme::Off : (AddHit(add, ID_ADD_TARGET) ? D2D1::ColorF(0x6A9DFF) : Theme::Accent), 8);
    Text(L"+  إضافة وجهة", g_fSegment.Get(), add, Theme::White);

    const float settingsH = 262;
    g_listRect = Rect(side.left + 8, y + 38, side.right - 8, side.bottom - settingsH);

    const float rowH = 62, gap = 6;
    const float contentH = g_targets.size() * (rowH + gap);
    const float listH    = g_listRect.bottom - g_listRect.top;
    g_scrollMax = std::max(0.0f, contentH - listH);
    g_scroll = std::clamp(g_scroll, 0.0f, g_scrollMax);

    if (g_targets.empty())
    {
        Text(L"لا توجد وجهات بعد", g_fCenter.Get(), Rect(g_listRect.left, g_listRect.top + 20, g_listRect.right, g_listRect.top + 50), Theme::SubText);
        Text(L"أضف يوتيوب أو تيك توك أو غيرها، ويمكنك البث إليها كلها معاً", g_fSmall.Get(),
             Rect(g_listRect.left + 8, g_listRect.top + 52, g_listRect.right - 8, g_listRect.top + 72), Theme::SubText);
    }

    g_rt->PushAxisAlignedClip(g_listRect, D2D1_ANTIALIAS_MODE_ALIASED);
    for (int i = 0; i < (int)g_targets.size(); ++i)
    {
        const StreamTarget& t = g_targets[i];
        const float top = g_listRect.top + i * (rowH + gap) - g_scroll;
        if (top + rowH < g_listRect.top || top > g_listRect.bottom)
            continue;
        const D2D1_RECT_F row = Rect(g_listRect.left, top, g_listRect.right, top + rowH);
        Fill(row, Theme::Card, 10);

        // شعار المنصة (مربع ملوّن)
        const D2D1_RECT_F badge = Rect(row.right - 50, row.top + 12, row.right - 12, row.top + 50);
        DrawPlatformIcon((int)t.platform, badge, t.enabled);

        Text(t.vertical ? t.name + L"  •  عمودي" : t.name, g_fBody.Get(), Rect(row.left + 96, row.top + 9, row.right - 60, row.top + 31),
             t.enabled ? Theme::Text : Theme::SubText);

        // السطر التاني: الحالة
        std::wstring sub;
        D2D1_COLOR_F subColor = Theme::SubText, dot = Theme::Off;
        const OutputStatus* st = TargetStatus(i);
        if (st)
        {
            switch (st->state)
            {
            case OutputState::Live:
            {
                wchar_t buf[96];
                swprintf_s(buf, L"مباشر • %d كيلوبت/ث", (int)std::lround(st->kbps));
                sub = buf;
                if (st->dropped > 0)
                    sub += L" • فُقد " + std::to_wstring(st->dropped) + L" إطار";
                subColor = Theme::Green; dot = Theme::Green;
                break;
            }
            case OutputState::Connecting:   sub = L"جارٍ الاتصال…"; subColor = dot = Theme::Yellow; break;
            case OutputState::Reconnecting: sub = L"إعادة الاتصال… " + st->message; subColor = dot = Theme::Yellow; break;
            case OutputState::Failed:       sub = st->message; subColor = dot = Theme::Red; break;
            default:                        sub = L"متوقف"; break;
            }
        }
        else if (!t.enabled)          sub = L"معطّلة";
        else if (t.url.empty())      { sub = L"أدخل عنوان الخادم من القائمة"; subColor = Theme::Yellow; }
        else if (t.key.empty())      { sub = L"أدخل مفتاح البث من القائمة"; subColor = Theme::Yellow; }
        else                          sub = L"جاهزة";
        if (st)
            Circle(row.right - 64, row.top + 42, 4, dot);
        Text(sub, g_fSmall.Get(), Rect(row.left + 96, row.top + 32, row.right - (st ? 74.0f : 60.0f), row.top + 52), subColor);

        // مفتاح التفعيل + زر القائمة
        const D2D1_RECT_F toggle = Rect(row.left + 10, row.top + 18, row.left + 54, row.top + 44);
        AddHit(toggle, ID_TARGET_TOGGLE_FIRST + i);
        DrawToggle(row.left + 12, row.top + 19, t.enabled, !live);

        const D2D1_RECT_F more = Rect(row.left + 58, row.top + 15, row.left + 90, row.top + 47);
        if (AddHit(more, ID_TARGET_MENU_FIRST + i) && !live)
            Fill(more, Theme::Hover, 8);
        Text(Icon::More, g_fIcon.Get(), more, live ? Theme::Off : Theme::SubText);
    }
    g_rt->PopAxisAlignedClip();

    // ---- إعدادات البث ----
    float sy = side.bottom - settingsH;
    Fill(Rect(side.left + 16, sy + 4, side.right - 16, sy + 5), Theme::Border);
    Text(L"دقة البث", g_fSection.Get(), Rect(L, sy + 12, R, sy + 34), Theme::SubText);
    DrawSegmented(Rect(L, sy + 36, R, sy + 70), { L"اللوحة", L"1080p", L"720p" }, g_streamRes, ID_STREAM_RES_FIRST, !live);
    UINT32 sw, sh;
    StreamSize(sw, sh);
    Text(SizeText((int)sw, (int)sh) + L"  •  " + std::to_wstring(std::min<UINT32>(CurrentFps(), 60)) + L" إطار/ث",
         g_fSmall.Get(), Rect(L, sy + 72, R, sy + 90), Theme::SubText);

    sy += 96;
    Text(L"البث العمودي (لتيك توك)", g_fSection.Get(), Rect(L, sy, R, sy + 22), Theme::SubText);
    sy += 24;
    DrawSegmented(Rect(L, sy, R, sy + 34), { L"قصّ المنتصف", L"الشاشة كاملة" }, g_verticalMode, ID_VERTICAL_FIRST, !live);
    sy += 42;
    Text(L"معدّل البت", g_fSection.Get(), Rect(L, sy, R, sy + 22), Theme::SubText);
    sy += 24;
    const D2D1_RECT_F box   = Rect(L, sy, R, sy + 34);
    const D2D1_RECT_F plus  = Rect(R - 40, sy + 3, R - 3, sy + 31);
    const D2D1_RECT_F minus = Rect(L + 3, sy + 3, L + 40, sy + 31);
    Fill(box, Theme::Card, 9);
    if (AddHit(plus, ID_STREAM_KBPS_PLUS) && !live)   Fill(plus, Theme::Hover, 7);
    if (AddHit(minus, ID_STREAM_KBPS_MINUS) && !live) Fill(minus, Theme::Hover, 7);
    Text(L"+", g_fCenter.Get(), plus,  live ? Theme::Off : Theme::Text);
    Text(L"−", g_fCenter.Get(), minus, live ? Theme::Off : Theme::Text);
    Text(std::to_wstring(g_streamKbps) + L" كيلوبت/ث", g_fCenter.Get(), Rect(L + 44, sy, R - 44, sy + 34), Theme::Text);
    sy += 38;

    // تنبيه: كل منصة بتاخد نفس السرعة من النت
    int enabled = 0;
    for (const StreamTarget& t : g_targets) if (t.enabled) ++enabled;
    std::wstring hint;
    if (enabled > 1)
        hint = L"سرعة الرفع المطلوبة ≈ " + std::to_wstring((g_streamKbps + 160) * enabled / 1000 + 1) + L" ميغابت/ث (" + std::to_wstring(enabled) + L" منصات)";
    else
        hint = L"يوتيوب ينصح بـ 4500–9000 لـ 1080p";
    Text(hint, g_fSmall.Get(), Rect(L, sy, R, sy + 18), Theme::SubText);
}

// ---------- الشريط الجانبي ----------
void DrawSidebar(const D2D1_RECT_F& side)
{
    Fill(side, Theme::Panel, 14);
    DrawSegmented(Rect(side.left + 12, side.top + 12, side.right - 12, side.top + 46),
                  { L"المصادر", L"البث" }, g_tab, ID_TAB_SOURCES, true);
    if (g_tab == 1) DrawStreamTab(side, side.top + 58);
    else            DrawSourcesTab(side, side.top + 58);
}

// ---------- الشريط السفلي ----------
void DrawBottomBar(const D2D1_RECT_F& bar)
{
    Fill(bar, Theme::Panel, 14);
    const bool rec    = g_recorder.IsRecording();
    const bool paused = g_recorder.IsPaused();
    const float cy = (bar.top + bar.bottom) / 2;
    const float cx = (bar.left + bar.right) / 2;

    // زر التسجيل الكبير (بالنص)
    const float R = 26;
    const D2D1_RECT_F recRect = Rect(cx - R, cy - R, cx + R, cy + R);
    Circle(cx, cy, R, AddHit(recRect, ID_RECORD) ? Theme::RedHover : Theme::Red);
    if (rec) Fill(Rect(cx - 9, cy - 9, cx + 9, cy + 9), Theme::White, 3);   // مربع = إيقاف
    else     Circle(cx, cy, 10, Theme::White);                               // دائرة = تسجيل

    // زر الإيقاف المؤقت
    const float pr = 20, px = cx - 66;
    const D2D1_RECT_F pauseRect = Rect(px - pr, cy - pr, px + pr, cy + pr);
    Circle(px, cy, pr, AddHit(pauseRect, ID_PAUSE) && rec ? Theme::Hover : Theme::Card);
    Text(paused ? Icon::Play : Icon::Pause, g_fIcon.Get(), pauseRect,
         rec ? (paused ? Theme::Yellow : Theme::Text) : Theme::Off);

    // زر البث المباشر
    const bool streaming = g_streamer.IsActive();
    const D2D1_RECT_F live = Rect(cx - 246, cy - 20, cx - 98, cy + 20);
    const bool liveHover = AddHit(live, ID_STREAM);
    if (streaming) Fill(live, liveHover ? D2D1::ColorF(0xFF5C78) : D2D1::ColorF(0xFF3B5C), 20);
    else           Fill(live, liveHover ? Theme::Hover : Theme::Card, 20);
    Text(Icon::Live, g_fIcon.Get(), Rect(live.right - 40, live.top, live.right - 12, live.bottom),
         streaming ? Theme::White : D2D1::ColorF(0xFF3B5C));
    Text(streaming ? L"إيقاف البث" : L"بدء البث", g_fCenter.Get(), Rect(live.left + 8, live.top, live.right - 40, live.bottom),
         streaming ? Theme::White : Theme::Text);

    // زر فتح المجلد
    const D2D1_RECT_F folder = Rect(bar.left + 16, cy - 20, bar.left + 56, cy + 20);
    Fill(folder, AddHit(folder, ID_FOLDER) ? Theme::Hover : Theme::Card, 10);
    Text(Icon::Folder, g_fIcon.Get(), folder, Theme::SubText);

    // زر الإعادة: مطفي = "تشغيل الإعادة" ، شغّال = "حفظ الإعادة" + زر صغير للإيقاف
    const bool replayOn = g_replay.IsActive();
    const D2D1_RECT_F rb = Rect(bar.left + 64, cy - 20, bar.left + 222, cy + 20);
    const bool rbHover = AddHit(rb, ID_REPLAY);
    if (replayOn) Fill(rb, rbHover ? WithAlpha(Theme::Accent, 0.32f) : WithAlpha(Theme::Accent, 0.18f), 10);
    else          Fill(rb, rbHover ? Theme::Hover : Theme::Card, 10);
    Text(Icon::Replay, g_fIcon.Get(), Rect(rb.right - 36, rb.top, rb.right - 8, rb.bottom), replayOn ? Theme::Accent : Theme::SubText);
    if (replayOn)
    {
        const D2D1_RECT_F stop = Rect(rb.left + 4, cy - 14, rb.left + 32, cy + 14);
        if (AddHit(stop, ID_REPLAY_STOP))
            Fill(stop, D2D1::ColorF(0xE5484D, 0.25f), 7);
        Text(Icon::Close, g_fIcon.Get(), stop, Theme::SubText);
        Text(L"حفظ الإعادة", g_fCenter.Get(), Rect(rb.left + 32, rb.top, rb.right - 36, rb.bottom), Theme::Text);
    }
    else
        Text(L"تشغيل الإعادة", g_fCenter.Get(), Rect(rb.left + 8, rb.top, rb.right - 36, rb.bottom), Theme::Text);

    // يمين: الرسالة + الاختصارات
    const D2D1_RECT_F info = Rect(cx + R + 24, bar.top, bar.right - 20, bar.bottom);
    if (!g_message.empty())
        Text(g_message, g_fBody.Get(), Rect(info.left, cy - 22, info.right, cy), g_messageIsError ? Theme::Red : Theme::Green);
    else if (streaming)
        Text(L"البث: " + std::to_wstring(LiveCount()) + L" من " + std::to_wstring(g_liveTargets.size()) + L" منصات على الهواء",
             g_fBody.Get(), Rect(info.left, cy - 22, info.right, cy), Theme::Text);
    else
        Text(rec ? L"المشفّر: " + g_recorder.EncoderName() : std::wstring(L"رتّب المشهد ثم اضغط الزر الأحمر"),
             g_fBody.Get(), Rect(info.left, cy - 22, info.right, cy), Theme::Text);
    {
        // الاختصارات الحالية (اللي إلها زر بس)
        std::wstring keys;
        const std::pair<int, const wchar_t*> shown[] = { { HK_RECORD, L"تسجيل" }, { HK_PAUSE, L"إيقاف مؤقت" },
                                                         { HK_STREAM, L"بث" }, { HK_REPLAY_SAVE, L"حفظ الإعادة" } };
        for (const auto& [action, label] : shown)
        {
            const std::wstring k = HotkeyManager::Text(g_hotkeys.Get(action));
            if (k.empty())
                continue;
            if (!keys.empty()) keys += L"  •  ";
            keys += LtrText(k) + L" " + label;
        }
        Text(keys, g_fSmall.Get(), Rect(info.left, cy + 2, info.right, cy + 22), Theme::SubText);
    }
}

// =============================================================
//  الرسم الكامل
// =============================================================
void CreateRenderTarget()
{
    if (g_rt)
        return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = props.dpiY = (float)g_dpi;
    g_d2d->CreateHwndRenderTarget(props,
        D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)), &g_rt);
    if (g_rt)
    {
        g_rt->CreateSolidColorBrush(Theme::Text, &g_brush);
        g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }
}

void DiscardRenderTarget()
{
    g_logoRt.Reset();
    for (auto& b : g_logoBmp) b.Reset();
    g_previewBitmap.Reset();
    g_previewValid = false;
    g_brush.Reset();
    g_rt.Reset();
}

void Paint()
{
    CreateRenderTarget();
    if (!g_rt)
        return;

    g_hits.clear();
    g_rt->BeginDraw();
    g_rt->Clear(Theme::Background);

    const D2D1_SIZE_F size = g_rt->GetSize();
    const float W = size.width, H = size.height;
    const float sideW = 350;

    // إذا في نافذة داخلية مفتوحة: الواجهة اللي تحتها ما بتتفاعل مع الماوس
    const D2D1_POINT_2F realMouse = g_mouse;
    if (g_panels.IsOpen())
        g_mouse = D2D1::Point2F(-1, -1);

    DrawTopBar(W);
    const D2D1_RECT_F side = Rect(W - 16 - sideW, 68, W - 16, H - 104);
    DrawPreview(Rect(16, 68, side.left - 16, H - 104));
    DrawSidebar(side);
    DrawBottomBar(Rect(16, H - 90, W - 16, H - 16));

    g_mouse = realMouse;
    g_panels.Draw(W, H);     // النافذة الداخلية فوق كلشي

    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET)
        DiscardRenderTarget();
}

// =============================================================
//  الماوس فوق المعاينة: تحديد، تحريك، تغيير الحجم
// =============================================================
int LayerAt(D2D1_POINT_2F p)
{
    int cw, ch;
    std::vector<SceneLayer> layers;
    g_scene.Snapshot(cw, ch, layers);
    for (auto it = layers.rbegin(); it != layers.rend(); ++it)     // من فوق لتحت
        if (it->visible && Inside(LayerToDip(*it, cw), p))
            return it->id;
    return 0;
}

int HandleAt(D2D1_POINT_2F p)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(g_selectedLayer);
    if (i < 0)
        return -1;
    const D2D1_RECT_F r = LayerToDip(g_scene.layers[i], g_scene.canvasW);
    for (int h = 0; h < 8; ++h)
    {
        const D2D1_POINT_2F hp = HandlePos(r, h);
        if (std::fabs(p.x - hp.x) <= 8 && std::fabs(p.y - hp.y) <= 8)
            return h;
    }
    return -1;
}

bool BeginDrag(D2D1_POINT_2F p)
{
    const D2D1_RECT_F zone = Rect(g_previewArea.left - 8, g_previewArea.top - 8, g_previewArea.right + 8, g_previewArea.bottom + 8);
    if (!Inside(zone, p))
        return false;

    const int handle = HandleAt(p);
    int id = g_selectedLayer;
    if (handle < 0)
    {
        id = LayerAt(p);
        g_selectedLayer = id;
        if (id == 0)
            return true;      // كبسة على مكان فاضي = إلغاء التحديد
    }

    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(id);
    if (i < 0)
        return true;
    g_dragOrig   = g_scene.layers[i];
    g_dragStart  = p;
    g_dragHandle = handle;
    g_drag       = handle >= 0 ? Drag::Resize : Drag::Move;
    SetCapture(g_hwnd);
    return true;
}

// -------------------------------------------------------------
// المغناطيس (Auto Snap): الحواف بتلزق على حواف اللوحة ونصها،
// وعلى حواف ونص المصادر التانية. Ctrl = بلا مغناطيس
// -------------------------------------------------------------
struct SnapEdges
{
    std::vector<double> x, y;     // كل الخطوط اللي فينا نلزق عليها
};

SnapEdges CollectEdges(int excludeId)   // لازم يكون القفل ممسوك
{
    const double cw = g_scene.canvasW, ch = g_scene.canvasH;
    SnapEdges e;
    e.x = { 0, cw / 2, cw };
    e.y = { 0, ch / 2, ch };
    for (const SceneLayer& l : g_scene.layers)
    {
        if (l.id == excludeId || !l.visible)
            continue;
        e.x.insert(e.x.end(), { l.x, l.x + l.w / 2, l.x + l.w });
        e.y.insert(e.y.end(), { l.y, l.y + l.h / 2, l.y + l.h });
    }
    return e;
}

// بيلزّق قيمة وحدة (حافة) على أقرب خط، وبيرجّع الخط إذا لزق
bool SnapValue(double& v, const std::vector<double>& lines, double range, double* guide)
{
    double best = range;
    bool hit = false;
    for (double line : lines)
    {
        const double d = std::fabs(v - line);
        if (d < best) { best = d; hit = true; if (guide) *guide = line; }
    }
    if (hit) v = *guide;
    return hit;
}

void UpdateDrag(D2D1_POINT_2F p, bool shift, bool noSnap, bool crop)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    g_guidesX.clear();
    g_guidesY.clear();
    const int i = FindLayer(g_dragOrig.id);
    if (i < 0)
        return;
    SceneLayer& l = g_scene.layers[i];
    const SceneLayer& o = g_dragOrig;
    const double s  = (g_previewDst.right - g_previewDst.left) / g_scene.canvasW;   // DIP لكل بكسل
    const double dx = (p.x - g_dragStart.x) / s;
    const double dy = (p.y - g_dragStart.y) / s;
    const double range = noSnap ? 0 : 8 / s;      // المغناطيس بيشتغل من بُعد 8 نقاط على الشاشة
    const SnapEdges edges = CollectEdges(o.id);

    if (g_drag == Drag::Move)
    {
        double x = o.x + dx, y = o.y + dy;

        // منجرّب نلزّق: يسار المصدر، نصّه، ويمينه - ومنختار الأقرب
        auto snapAxis = [&](double& pos, double size, const std::vector<double>& lines, std::vector<double>& guides)
        {
            double bestD = range, bestPos = pos, bestLine = 0;
            bool hit = false;
            for (double offset : { 0.0, size / 2, size })
                for (double line : lines)
                {
                    const double d = std::fabs(pos + offset - line);
                    if (d < bestD) { bestD = d; bestPos = line - offset; bestLine = line; hit = true; }
                }
            if (hit)
            {
                pos = bestPos;
                guides.push_back(bestLine);
            }
        };
        snapAxis(x, o.w, edges.x, g_guidesX);
        snapAxis(y, o.h, edges.y, g_guidesY);
        l.x = x;
        l.y = y;
        return;
    }

    // ---- تغيير الحجم ----
    const int h = g_dragHandle;
    const bool L = (h == 0 || h == 6 || h == 7), R = (h == 2 || h == 3 || h == 4);
    const bool T = (h == 0 || h == 1 || h == 2), B = (h == 4 || h == 5 || h == 6);
    double left = o.x, top = o.y, right = o.x + o.w, bottom = o.y + o.h;
    if (L) left += dx;
    if (R) right += dx;
    if (T) top += dy;
    if (B) bottom += dy;

    // الحافة اللي عم تتحرك بتلزق
    double g = 0;
    if (L && SnapValue(left,   edges.x, range, &g)) g_guidesX.push_back(g);
    if (R && SnapValue(right,  edges.x, range, &g)) g_guidesX.push_back(g);
    if (T && SnapValue(top,    edges.y, range, &g)) g_guidesY.push_back(g);
    if (B && SnapValue(bottom, edges.y, range, &g)) g_guidesY.push_back(g);

    // ---- القص (Alt): الحافة بتتحرك بس المحتوى بيضل مكانه وبنفس حجمه ----
    if (crop)
    {
        // حجم المصدر كامل (قبل القص) على اللوحة
        const double fw = o.w / std::max(0.05, 1 - o.cropL - o.cropR);
        const double fh = o.h / std::max(0.05, 1 - o.cropT - o.cropB);
        const double minW = 16 / fw, minH = 16 / fh;            // أصغر شي 16 بكسل
        double cl = o.cropL, cr = o.cropR, ct = o.cropT, cb = o.cropB;
        // ما منقص أكتر من المصدر، وما منكبّر أكبر منه (القص ما بيصير سالب)
        if (L) cl = std::clamp(o.cropL + (left - o.x) / fw, 0.0, 1 - cr - minW);
        if (R) cr = std::clamp(o.cropR + (o.x + o.w - right) / fw, 0.0, 1 - cl - minW);
        if (T) ct = std::clamp(o.cropT + (top - o.y) / fh, 0.0, 1 - cb - minH);
        if (B) cb = std::clamp(o.cropB + (o.y + o.h - bottom) / fh, 0.0, 1 - ct - minH);
        // مكان المصدر الكامل ثابت، ومنه منحسب الطبقة
        const double fx = o.x - o.cropL * fw, fy = o.y - o.cropT * fh;
        l.cropL = cl;  l.cropR = cr;  l.cropT = ct;  l.cropB = cb;
        l.x = fx + cl * fw;
        l.y = fy + ct * fh;
        l.w = fw * (1 - cl - cr);
        l.h = fh * (1 - ct - cb);
        return;
    }
    l.cropL = o.cropL;  l.cropR = o.cropR;  l.cropT = o.cropT;  l.cropB = o.cropB;

    // الزوايا بتحافظ على النسبة (إلا إذا ضاغط Shift)
    if ((L || R) && (T || B) && !shift && o.h > 0)
    {
        const double aspect = o.w / o.h;
        double nw = right - left, nh = bottom - top;
        if (std::fabs(nw / o.w) > std::fabs(nh / o.h)) { nh = nw / aspect; g_guidesY.clear(); }
        else                                           { nw = nh * aspect; g_guidesX.clear(); }
        if (L) left = right - nw; else right = left + nw;
        if (T) top = bottom - nh; else bottom = top + nh;
    }

    // أصغر حجم 16 بكسل
    if (right - left < 16) { if (L) left = right - 16; else right = left + 16; }
    if (bottom - top < 16) { if (T) top = bottom - 16; else bottom = top + 16; }
    l.x = left;
    l.y = top;
    l.w = right - left;
    l.h = bottom - top;
}

void NudgeSelected(double dx, double dy)
{
    std::lock_guard<std::mutex> lock(g_scene.mutex);
    const int i = FindLayer(g_selectedLayer);
    if (i >= 0)
    {
        g_scene.layers[i].x += dx;
        g_scene.layers[i].y += dy;
    }
}

// =============================================================
//  الأحداث
// =============================================================
// =============================================================
//  أيقونة جنب الساعة
// =============================================================
void ShowFromTray()
{
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

void HideToTray()
{
    g_panels.Close();
    ShowWindow(g_hwnd, SW_HIDE);
    if (!g_trayHintShown)       // أول مرة بس: منخبّره وين صار
    {
        g_tray.Balloon(L"مسجّل الشاشة ما زال يعمل",
                       L"البرنامج هنا بجانب الساعة. اضغط على أيقونته لفتحه، أو بالزر الأيمن للخروج.");
        g_trayHintShown = true;
        SaveSettings();
    }
}

void QuitApp()
{
    g_reallyQuit = true;
    DestroyWindow(g_hwnd);       // إذا عم يسجّل بينحفظ الملف أول (WM_DESTROY)
}

void OpenSettings(int page);   // تحت

void ShowTrayMenu(POINT pt)
{
    const bool rec = g_recorder.IsRecording(), live = g_streamer.IsActive(), replay = g_replay.IsActive();
    std::vector<std::wstring> items;
    std::vector<int> codes;
    auto add = [&](const std::wstring& t, int c) { items.push_back(t); codes.push_back(c); };
    add(L"فتح مسجّل الشاشة", 0);
    add(L"الإعدادات", 6);
    add(L"", -1);
    add(rec ? L"إيقاف التسجيل" : L"بدء التسجيل", 1);
    if (rec)
        add(g_recorder.IsPaused() ? L"استئناف التسجيل" : L"إيقاف مؤقت", 2);
    add(live ? L"إيقاف البث" : L"بدء البث", 3);
    if (replay)
        add(L"حفظ الإعادة", 4);
    add(replay ? L"إيقاف الإعادة" : L"تشغيل الإعادة", 5);
    add(L"", -1);
    add(L"خروج", 9);

    SetForegroundWindow(g_hwnd);     // لازم لحتى تتسكّر القائمة لما تكبس برّاها
    const int pick = ShowMenu(items, -1, pt);
    if (pick < 0 || codes[pick] < 0)
        return;
    switch (codes[pick])
    {
    case 0: ShowFromTray(); break;
    case 1: ToggleRecording(); break;
    case 2: TogglePause(); break;
    case 3: ToggleStreaming(); break;
    case 4: SaveReplay(); break;
    case 5: if (replay) StopReplay(); else StartReplay(); break;
    case 6: ShowFromTray(); OpenSettings(g_settingsPage); break;
    case 9: QuitApp(); break;
    }
}

std::unique_ptr<Panel> MakeReplayPanel()
{
    ReplayPanelHooks h;
    h.changed = [](bool restart)
    {
        g_replayBuffer->SetSeconds(REPLAY_SECONDS[g_replaySet.seconds]);
        SaveSettings();
        if (restart)
            RestartReplayIfActive();
    };
    h.pickFolder = []
    {
        std::wstring path = ReplayFolder();
        if (!PickFolder(path, L"اختر مكان حفظ الإعادات"))
            return false;
        g_replaySet.folder = path;
        return true;
    };
    h.openFolder = [] { OpenFolderPath(ReplayFolder()); };
    h.folderPath = [] { return ReplayFolder(); };
    h.monitors   = []
    {
        std::vector<std::wstring> names;
        int n = 0;
        for (const CaptureTarget& t : ReplayMonitors())
            names.push_back(L"الشاشة " + std::to_wstring(++n) + (t.isPrimary ? L" (الرئيسية)" : L""));
        return names;
    };
    h.micEnabled = [] { return g_micOn; };
    h.sourceW = []
    {
        if (g_replaySet.source == 0)
        {
            const auto mons = ReplayMonitors();
            if (!mons.empty())
            {
                const RECT& r = mons[std::clamp(g_replaySet.monitor, 0, (int)mons.size() - 1)].rect;
                return (int)(r.right - r.left);
            }
        }
        return g_scene.canvasW;
    };
    h.sourceH = []
    {
        if (g_replaySet.source == 0)
        {
            const auto mons = ReplayMonitors();
            if (!mons.empty())
            {
                const RECT& r = mons[std::clamp(g_replaySet.monitor, 0, (int)mons.size() - 1)].rect;
                return (int)(r.bottom - r.top);
            }
        }
        return g_scene.canvasH;
    };
    return std::make_unique<ReplayPanel>(g_replaySet, std::move(h));
}

std::unique_ptr<Panel> MakeHotkeysPanel()
{
    return std::make_unique<HotkeysPanel>(g_hotkeys, []
    {
        g_hotkeys.Save(g_settingsPath);       // الإجراءات
        SceneHotkeysFromManager();            // المشاهد بتنحفظ مع المشاهد
        SaveScenes();
        g_hotkeys.Apply(g_hwnd);
    });
}

// نافذة الإعدادات (كل الإعدادات بمكان واحد، شوف SettingsPanel.h)
void OnClick(int id, D2D1_POINT_2F p);
void OpenSettings(int page)
{
    SettingsHooks h;
    h.drawPage = [](int pg, float L, float top, float R) { return DrawSettingsPage(pg, L, top, R); };
    h.click = [](int id)
    {
        // الكبسة جاية من نافذة الإعدادات: منحسب مكان الماوس (للقوائم المنبثقة متل المشفّر)
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(g_hwnd, &pt);
        const float k = 96.0f / g_dpi;
        OnClick(id, D2D1::Point2F(pt.x * k, pt.y * k));
    };
    g_settingsPage = page;
    g_panels.Open(std::make_unique<SettingsPanel>(std::move(h), MakeReplayPanel(), MakeHotkeysPanel(), page));
}

void OnClick(int id, D2D1_POINT_2F p)
{
    const bool rec = Busy();
    const POINT screen = DipToScreen(p.x, p.y);

    // أصوات البرامج
    if (id >= ID_APP_ROW_FIRST && id < ID_APP_LAST)
    {
        if (id >= ID_APP_VOL_FIRST)
            return;                                   // الشريط بينسحب (مش كبسة)
        if (id >= ID_APP_DELETE_FIRST)
        {
            g_appMixer.Remove(id - ID_APP_DELETE_FIRST);
            g_appLevels.clear();
            SaveScenes();
        }
        else if (id >= ID_APP_MUTE_FIRST)
        {
            const int i = id - ID_APP_MUTE_FIRST;
            g_appMixer.SetMuted(i, !g_appMixer.Entry(i).muted);
            SaveScenes();
        }
        return;
    }

    if (id >= ID_TARGET_MENU_FIRST)   { ShowTargetMenu(id - ID_TARGET_MENU_FIRST, screen); return; }
    if (id >= ID_TARGET_TOGGLE_FIRST) { ToggleTarget(id - ID_TARGET_TOGGLE_FIRST); return; }
    if (id >= ID_SCENE_FIRST)  { SwitchScene(id - ID_SCENE_FIRST); return; }
    if (id >= ID_DELETE_FIRST) { RemoveLayer(id - ID_DELETE_FIRST); return; }
    if (id >= ID_EYE_FIRST)    { LayerCommand(id - ID_EYE_FIRST, 8); return; }
    if (id >= ID_LAYER_FIRST)  { g_selectedLayer = id - ID_LAYER_FIRST; return; }

    if (id >= ID_CANVAS_FIRST && id < ID_CANVAS_FIRST + 4)
    {
        if (!rec) { g_canvasChoice = id - ID_CANVAS_FIRST; ApplyCanvasChoice(); SaveSettings(); RestartReplayIfActive(); }
        return;
    }
    if (id >= ID_FPS_FIRST && id < ID_FPS_FIRST + 4)
    {
        if (!rec) { g_fpsChoice = id - ID_FPS_FIRST; SaveSettings(); RestartReplayIfActive(); }
        return;
    }
    if (id >= ID_STARTUP_FIRST && id < ID_STARTUP_FIRST + 2)
    {
        SetRunAtStartup(id == ID_STARTUP_FIRST + 1);
        return;
    }
    if (id == ID_FOLDER_CHANGE)
    {
        if (rec) return;
        std::wstring path = g_folder;
        if (PickFolder(path, L"اختر مكان حفظ التسجيلات"))
        {
            g_folder = path;
            SaveSettings();
            ShowMessage(L"ستُحفظ التسجيلات في: " + path, false);
        }
        return;
    }
    if (id == ID_FOLDER_OPEN)
    {
        OpenFolderPath(g_folder);
        return;
    }
    if (id == ID_REPLAY_SETTINGS) { OpenSettings(SP_REPLAY); return; }
    if (id == ID_OPEN_SETTINGS || id == ID_TAB_SETTINGS) { OpenSettings(g_settingsPage); return; }
    if (id == ID_OVERAPP_FIRST || id == ID_OVERAPP_SECOND)
    {
        g_overlayOverApp = (id == ID_OVERAPP_FIRST);
        SaveSettings();
        return;
    }
    if (id >= ID_TOASTS_FIRST && id < ID_TOASTS_FIRST + 2)
    {
        g_toastsOn = (id == ID_TOASTS_FIRST);
        SaveSettings();
        return;
    }
    if (id == ID_TEST_TOAST)
    {
        // تجربة: بيطلع لما تصغّر البرنامج (وهو قدامك الإشعارات ما بتطلع)
        g_overlay.Toast(Glyph::Check, Theme::Green, L"هكذا تبدو الإشعارات", L"تظهر في زاوية المؤشر ولا تظهر في التسجيل", true);
        return;
    }
    if (id >= ID_CLOSE_FIRST && id < ID_CLOSE_FIRST + 2)
    {
        g_closeToTray = (id == ID_CLOSE_FIRST);
        SaveSettings();
        return;
    }
    if (id == ID_HOTKEYS) { OpenSettings(SP_HOTKEYS); return; }
    if (id >= ID_OVERLAY_FIRST && id < ID_OVERLAY_FIRST + 5)
    {
        g_overlayCorner = id - ID_OVERLAY_FIRST;
        SaveSettings();
        return;
    }
    if (id >= ID_AUDIO_TRACKS_FIRST && id < ID_AUDIO_TRACKS_FIRST + 2)
    {
        if (!rec) { g_separateTracks = (id == ID_AUDIO_TRACKS_FIRST + 1); SaveSettings(); }
        return;
    }
    if (id >= ID_CONTAINER_FIRST && id < ID_CONTAINER_FIRST + 2)
    {
        if (!rec) { g_crashSafe = (id == ID_CONTAINER_FIRST); SaveSettings(); }
        return;
    }
    if (id >= ID_TRANSITION_FIRST && id < ID_TRANSITION_FIRST + 3)
    {
        g_transitionChoice = id - ID_TRANSITION_FIRST;
        SaveSettings();
        return;
    }
    if (id >= ID_VERTICAL_FIRST && id < ID_VERTICAL_FIRST + 2)
    {
        if (!g_streamer.IsActive()) { g_verticalMode = id - ID_VERTICAL_FIRST; SaveSettings(); }
        return;
    }
    if (id >= ID_STREAM_RES_FIRST && id < ID_STREAM_RES_FIRST + 3)
    {
        if (!g_streamer.IsActive()) { g_streamRes = id - ID_STREAM_RES_FIRST; SaveSettings(); }
        return;
    }
    if (id >= ID_QUALITY_FIRST && id < ID_QUALITY_FIRST + 4)
    {
        if (!rec) { g_quality = id - ID_QUALITY_FIRST; SaveSettings(); RestartReplayIfActive(); }
        return;
    }
    switch (id)
    {
    case ID_ADD_SCENE:     AddScene(); SaveScenes(); break;
    case ID_SYSTEM_MUTE:   g_systemAudio.SetMuted(!g_systemAudio.IsMuted()); SaveSettings(); break;
    case ID_MIC_MUTE:      g_micAudio.SetMuted(!g_micAudio.IsMuted()); SaveSettings(); break;
    case ID_TAB_SOURCES:   g_tab = 0; g_scroll = 0; g_scrollMax = 0; break;
    case ID_TAB_STREAM:    g_tab = 1; g_scroll = 0; g_scrollMax = 0; break;
    case ID_TAB_SETTINGS:  g_tab = 2; g_scroll = 0; g_scrollMax = 0; break;
    case ID_REPLAY:        if (g_replay.IsActive()) SaveReplay(); else StartReplay(); break;
    case ID_REPLAY_STOP:   StopReplay(); break;
    case ID_MIC_FILTERS:   ShowMicFilters(); break;
    case ID_STREAM:        ToggleStreaming(); break;
    case ID_ADD_TARGET:    AddTarget(screen); break;
    case ID_STREAM_KBPS_MINUS:
        if (!g_streamer.IsActive()) { g_streamKbps = std::max<UINT32>(1000, g_streamKbps - 500); SaveSettings(); }
        break;
    case ID_STREAM_KBPS_PLUS:
        if (!g_streamer.IsActive()) { g_streamKbps = std::min<UINT32>(20000, g_streamKbps + 500); SaveSettings(); }
        break;
    case ID_BITRATE_MINUS: if (!rec) { ChangeBitrate(-1); SaveSettings(); } break;
    case ID_BITRATE_PLUS:  if (!rec) { ChangeBitrate(+1); SaveSettings(); } break;
    case ID_ENCODER_MENU:  if (!rec) { ShowEncoderMenu(screen); RestartReplayIfActive(); } break;
    case ID_RECORD:        ToggleRecording(); break;
    case ID_PAUSE:         TogglePause();     break;
    case ID_FOLDER:        OpenFolder();      break;
    case ID_ADD_SOURCE:    ShowAddSourceMenu(screen); break;
    case ID_SYSTEM_AUDIO:  SetAudioEnabled(g_systemAudio, g_systemOn, !g_systemOn); break;
    case ID_MIC:           SetAudioEnabled(g_micAudio, g_micOn, !g_micOn); break;
    case ID_SYSTEM_DEVICE: ShowDeviceMenu(g_systemAudio, g_systemOn, AudioSource::Kind::System, screen); break;
    case ID_MIC_DEVICE:    ShowDeviceMenu(g_micAudio, g_micOn, AudioSource::Kind::Microphone, screen); break;
    }
}

D2D1_POINT_2F ToDip(LPARAM lParam)
{
    const float s = 96.0f / g_dpi;
    return D2D1::Point2F(GET_X_LPARAM(lParam) * s, GET_Y_LPARAM(lParam) * s);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hwnd = hwnd;
        g_dpi  = GetDpiForWindow(hwnd);
        CreateAppIcons();
        g_tray.Add(hwnd, g_icons[0][1], L"مسجّل الشاشة");
        UpdateAppIcon();
        ApplyCanvasChoice();
        // المشاهد المحفوظة من آخر مرة، وإذا ما في: مشهد واحد فيه الشاشة الرئيسية
        if (!LoadScenes())
        {
            g_scenes.push_back({ L"المشهد 1", {} });
            g_activeScene = 0;
            for (const CaptureTarget& t : ListCaptureTargets(hwnd))
                if (t.isMonitor && t.isPrimary)
                    AddLayer(t);
            g_selectedLayer = 0;
        }
        // أصوات البرامج: الخلّاط شغّال دايماً، وبرامج المشهد الحالي
        g_appMixer.Start();
        if (g_activeScene < (int)g_scenes.size())
            g_appMixer.SetEntries(g_scenes[g_activeScene].apps);
        SetTimer(hwnd, TIMER_FRAME, 33, nullptr);
        SyncSceneHotkeys();         // اختصارات المشاهد
        g_hotkeys.Apply(hwnd);
        LoadTargets();
        return 0;
    }

    case WM_TIMER:
        ++g_tick;
        g_systemLevel = std::max(LevelToMeter(g_systemAudio.TakePeak()), g_systemLevel * 0.88f);
        g_micLevel    = std::max(LevelToMeter(g_micAudio.TakePeak()),    g_micLevel * 0.88f);
        {
            // مؤشر التسجيل الصغير (فوق الألعاب وسطح المكتب)
            OverlayState os;
            os.recording = g_recorder.IsRecording();
            os.paused    = g_recorder.IsPaused();
            os.streaming = g_streamer.IsActive();
            os.elapsedMs = os.recording ? g_recorder.ElapsedMs() : (os.streaming ? g_streamer.ElapsedMs() : 0);
            os.micOn     = g_micOn && g_micAudio.IsRunning();
            os.micMuted  = g_micAudio.IsMuted();
            os.micLevel  = g_micLevel;
            os.replay      = g_replay.IsActive();
            os.replaySaved = g_tick - g_replaySavedTick < 60;     // حوالي ثانيتين
            // الشاشة: اللي عم تتسجّل. إذا بس الإعادة شغّالة على "الشاشة كاملة": شاشتها
            os.monitor     = (!os.recording && !os.streaming && os.replay && g_replaySet.source == 0 && g_replayMonitorHandle)
                             ? g_replayMonitorHandle : RecordedMonitor();
            // owner = فاضي: بيبيّنوا دايماً، حتى فوق نافذة البرنامج
            g_overlay.Update(os, g_overlayCorner, g_overlayOverApp ? nullptr : hwnd);
        }
        g_appLevels.resize(g_appMixer.Count(), 0.0f);
        for (size_t i = 0; i < g_appLevels.size(); ++i)
            g_appLevels[i] = std::max(LevelToMeter(g_appMixer.TakePeak((int)i)), g_appLevels[i] * 0.88f);
        // المعاينة: 10 مرات بالثانية (ووقت التسجيل 5 لنوفّر المعالج)
        if (g_fadeTicks > 0)            // وقت التلاشي: المعاينة كل إطار لتبيّن ناعمة
        {
            --g_fadeTicks;
            UpdatePreview();
        }
        else if (g_tick % (Busy() ? 6 : 3) == 0)
            UpdatePreview();
        if (g_tick == 20 && g_replaySet.autoStart && !g_replay.IsActive())   // تشغيل تلقائي (من إعدادات الإعادة)
            StartReplay(true);
        if (g_streamer.IsActive())
            g_streamStatus = g_streamer.Status();
        if (g_tick % 30 == 0 && (g_recorder.IsRecording() || g_streamer.IsActive()))   // وقت التسجيل بتلميح الأيقونة
            g_tray.SetTip((g_recorder.IsRecording() ? L"● جارٍ التسجيل  " + FormatTime(g_recorder.ElapsedMs())
                                                    : L"● بث مباشر  " + FormatTime(g_streamer.ElapsedMs())) + L"\nمسجّل الشاشة");
        if (g_tick % 60 == 0)           // كل ثانيتين: النوافذ والبرامج اللي رجعت انفتحت
        {
            ReconnectLostWindows();
            g_appMixer.Reconnect();
        }
        if (g_tick % 150 == 0 && g_drag == Drag::None)   // كل 5 ثواني منحفظ المشاهد
            SaveScenes();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        Paint();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        if (g_rt)
            g_rt->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        g_previewValid = false;
        return 0;

    case WM_DPICHANGED:
    {
        g_dpi = HIWORD(wParam);
        DiscardRenderTarget();
        const RECT* r = (const RECT*)lParam;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        auto* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = MulDiv(980, g_dpi, 96);
        mm->ptMinTrackSize.y = MulDiv(760, g_dpi, 96);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        g_mouse = ToDip(lParam);
        if (g_panels.IsOpen())
            g_panels.OnMouseMove(g_mouse);
        if (g_volDrag)
            SetVolumeFromX(*g_volDrag, g_volTrack[g_volDrag == &g_systemAudio ? 0 : 1], g_mouse.x);
        if (g_appVolDrag >= 0)
            g_appMixer.SetVolume(g_appVolDrag, VolumeAtX(g_appVolTrack, g_mouse.x));
        if (g_drag != Drag::None)
            UpdateDrag(g_mouse, (wParam & MK_SHIFT) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                       (GetKeyState(VK_MENU) & 0x8000) != 0);
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_drag == Drag::None)
            g_mouse = D2D1::Point2F(-1, -1);
        return 0;

    case WM_LBUTTONDOWN:
    {
        const D2D1_POINT_2F p = ToDip(lParam);
        if (g_panels.IsOpen())
        {
            g_panels.OnMouseDown(p);
            if (g_panels.IsDragging())
                SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;
        }
        const int hit = HitAt(p);
        if (hit == ID_SYSTEM_VOL || hit == ID_MIC_VOL)     // بلّش سحب شريط العلو
        {
            const int index = (hit == ID_SYSTEM_VOL) ? 0 : 1;
            g_volDrag = index == 0 ? &g_systemAudio : &g_micAudio;
            SetVolumeFromX(*g_volDrag, g_volTrack[index], p.x);
            SetCapture(hwnd);
            return 0;
        }
        if (hit >= ID_APP_VOL_FIRST && hit < ID_APP_LAST)   // شريط علو برنامج
        {
            const int i = hit - ID_APP_VOL_FIRST;
            if (i < (int)g_appTracks.size())
            {
                g_appVolDrag  = i;
                g_appVolTrack = g_appTracks[i];
                g_appMixer.SetVolume(i, VolumeAtX(g_appVolTrack, p.x));
                SetCapture(hwnd);
            }
            return 0;
        }
        if (hit == 0)          // مش فوق زر: يمكن فوق المعاينة
            BeginDrag(p);
        SetFocus(hwnd);
        return 0;
    }

    case WM_LBUTTONDBLCLK:   // دبل كليك على مشهد = إعادة تسمية
    {
        const D2D1_POINT_2F p = ToDip(lParam);
        if (g_panels.IsOpen())       // جوّا النافذة الداخلية: متل الكبسة العادية
        {
            g_panels.OnMouseDown(p);
            if (g_panels.IsDragging())
                SetCapture(hwnd);
            return 0;
        }
        const int hit = HitAt(p);
        if (hit >= ID_SCENE_FIRST && hit < ID_TARGET_TOGGLE_FIRST)
        {
            g_ignoreNextUp = true;
            RenameScene(hit - ID_SCENE_FIRST);
            return 0;
        }
        if (hit >= ID_APP_VOL_FIRST && hit < ID_APP_LAST)  // دبل كليك على شريط برنامج = 100%
        {
            g_appMixer.SetVolume(hit - ID_APP_VOL_FIRST, 1.0f);
            g_ignoreNextUp = true;
            SaveScenes();
            return 0;
        }
        if (hit == ID_SYSTEM_VOL || hit == ID_MIC_VOL)     // دبل كليك على الشريط = رجوع لـ 100%
        {
            (hit == ID_SYSTEM_VOL ? g_systemAudio : g_micAudio).SetVolume(1.0f);
            g_ignoreNextUp = true;
            SaveSettings();
            return 0;
        }
        if (hit == 0)
            BeginDrag(p);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (g_ignoreNextUp)
        {
            g_ignoreNextUp = false;
            return 0;
        }
        if (g_panels.IsOpen())
        {
            if (g_panels.IsDragging())
                ReleaseCapture();
            g_panels.OnMouseUp(ToDip(lParam));
            return 0;
        }
        if (g_volDrag)
        {
            g_volDrag = nullptr;
            ReleaseCapture();
            SaveSettings();
            return 0;
        }
        if (g_appVolDrag >= 0)
        {
            g_appVolDrag = -1;
            ReleaseCapture();
            SaveScenes();
            return 0;
        }
        if (g_drag != Drag::None)
        {
            g_drag = Drag::None;
            g_guidesX.clear();
            g_guidesY.clear();
            ReleaseCapture();
            return 0;
        }
        const D2D1_POINT_2F p = ToDip(lParam);
        if (const int id = HitAt(p))
            OnClick(id, p);
        return 0;
    }

    case WM_RBUTTONUP:   // قائمة خيارات الطبقة
    {
        if (g_panels.IsOpen())
            return 0;
        const D2D1_POINT_2F p = ToDip(lParam);
        int id = 0;
        const int hit = HitAt(p);
        if (hit >= ID_SCENE_FIRST && hit < ID_TARGET_TOGGLE_FIRST)
        {
            ShowSceneMenu(hit - ID_SCENE_FIRST, DipToScreen(p.x, p.y));
            return 0;
        }
        if (hit >= ID_LAYER_FIRST && hit < ID_EYE_FIRST) id = hit - ID_LAYER_FIRST;
        else if (Inside(g_previewArea, p))                id = LayerAt(p);
        if (id)
        {
            g_selectedLayer = id;
            ShowLayerMenu(id, DipToScreen(p.x, p.y));
        }
        return 0;
    }

    case WM_CHAR:          // كتابة بالنوافذ الداخلية (مثلاً خانة البحث)
        if (g_panels.IsOpen())
        {
            g_panels.OnChar((wchar_t)wParam);
            return 0;
        }
        break;

    case WM_KEYDOWN:
    {
        if (g_panels.IsOpen())       // النافذة الداخلية بتاخد الكيبورد (Esc = إغلاق)
        {
            g_panels.OnKey(wParam);
            return 0;
        }
        const double step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
        switch (wParam)
        {
        case VK_DELETE: if (g_selectedLayer) RemoveLayer(g_selectedLayer); break;
        default:
            if (wParam >= '1' && wParam <= '9')          // 1..9 = تبديل المشهد
                SwitchScene((int)(wParam - '1'));
            break;
        case VK_LEFT:   NudgeSelected(-step, 0); break;
        case VK_RIGHT:  NudgeSelected(step, 0);  break;
        case VK_UP:     NudgeSelected(0, -step); break;
        case VK_DOWN:   NudgeSelected(0, step);  break;
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        const float s = 96.0f / g_dpi;
        const D2D1_POINT_2F mp = D2D1::Point2F(pt.x * s, pt.y * s);
        const float steps = GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f;
        if (g_panels.IsOpen())
        {
            g_panels.OnWheel(mp, steps);
            return 0;
        }
        // عجلة الماوس فوق شريط العلو: ±5% (بس بتبويب المصادر، الأشرطة ما بتبيّن غير فيه)
        for (int i = 0; g_tab == 0 && i < (int)g_appTracks.size(); ++i)
        {
            const D2D1_RECT_F t = g_appTracks[i];
            if (t.left >= 0 && Inside(g_listRect, mp) && Inside(Rect(t.left - 8, t.top - 12, t.right + 8, t.bottom + 12), mp))
            {
                const float v = g_appMixer.Entry(i).volume;
                g_appMixer.SetVolume(i, std::round((v + steps * 0.05f) * 20.0f) / 20.0f);
                SaveScenes();
                return 0;
            }
        }
        for (int i = 0; g_tab == 0 && i < 2; ++i)
        {
            const D2D1_RECT_F t = g_volTrack[i];
            if (Inside(Rect(t.left - 8, t.top - 12, t.right + 8, t.bottom + 12), mp))
            {
                AudioSource& src = i == 0 ? g_systemAudio : g_micAudio;
                src.SetVolume(std::round((src.Volume() + steps * 0.05f) * 20.0f) / 20.0f);
                SaveSettings();
                return 0;
            }
        }
        if (Inside(g_listRect, mp))
            g_scroll = std::clamp(g_scroll - steps * 54.0f, 0.0f, g_scrollMax);
        return 0;

    }

    // Alt (للقص): ويندوز عادةً بيفتح قائمة النافذة لما تترك Alt، منمنعه
    // (Alt+F4 بيضل شغّال لأنه مفتاح تاني)
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        // نافذة الاختصارات عم تستنى زر: بتاخد حتى Alt و F10
        if (g_panels.WantsKeys())
        {
            if (msg == WM_SYSKEYDOWN)
                g_panels.OnKey(wParam);
            return 0;
        }
        if (wParam == VK_MENU)
            return 0;
        break;
    case WM_SYSCHAR:
        if (g_panels.WantsKeys())
            return 0;          // بلا صوت "تنبيه" لما تكبس Alt + حرف
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU && lParam == 0)
            return 0;
        break;

    case WM_SETCURSOR:   // شكل الماوس حسب المكان
        if (LOWORD(lParam) == HTCLIENT && g_panels.IsOpen())
        {
            SetCursor(LoadCursor(nullptr, g_panels.WantsHandCursor(g_mouse) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        if (LOWORD(lParam) == HTCLIENT)
        {
            LPCWSTR cursor = nullptr;
            const int handle = HandleAt(g_mouse);
            if (g_drag == Drag::Resize) cursor = (g_dragHandle == 1 || g_dragHandle == 5) ? IDC_SIZENS
                                               : (g_dragHandle == 3 || g_dragHandle == 7) ? IDC_SIZEWE
                                               : (g_dragHandle == 0 || g_dragHandle == 4) ? IDC_SIZENWSE : IDC_SIZENESW;
            else if (g_drag == Drag::Move) cursor = IDC_SIZEALL;
            else if (handle >= 0)          cursor = (handle == 1 || handle == 5) ? IDC_SIZENS
                                                  : (handle == 3 || handle == 7) ? IDC_SIZEWE
                                                  : (handle == 0 || handle == 4) ? IDC_SIZENWSE : IDC_SIZENESW;
            else if (HitAt(g_mouse) != 0)  cursor = IDC_HAND;
            else if (Inside(g_previewArea, g_mouse) && LayerAt(g_mouse) != 0) cursor = IDC_SIZEALL;
            if (cursor)
            {
                SetCursor(LoadCursor(nullptr, cursor));
                return TRUE;
            }
        }
        break;

    case WM_HOTKEY:
    {
        const int slot = HotkeyManager::SlotFromId(wParam);
        if (slot >= HK_COUNT)                     // اختصار مشهد
        {
            SwitchScene(slot - HK_COUNT);
            return 0;
        }
        switch (slot)
        {
        case HK_RECORD:        ToggleRecording(); break;
        case HK_PAUSE:         TogglePause(); break;
        case HK_STREAM:        ToggleStreaming(); break;
        case HK_REPLAY_SAVE:   SaveReplay(); break;
        case HK_REPLAY_TOGGLE: if (g_replay.IsActive()) StopReplay(); else StartReplay(); break;
        case HK_MIC_MUTE:
            g_micAudio.SetMuted(!g_micAudio.IsMuted());
            ShowMessage(g_micAudio.IsMuted() ? L"كُتم الميكروفون" : L"أُلغي كتم الميكروفون", false);
            if (g_micAudio.IsMuted()) Notify(Glyph::MicOff, Theme::Red, L"الميكروفون مكتوم");
            else                      Notify(Glyph::Mic, Theme::Green, L"الميكروفون يعمل");
            SaveSettings();
            break;
        case HK_NEXT_SCENE:
            if (!g_scenes.empty())
                SwitchScene((g_activeScene + 1) % (int)g_scenes.size());
            break;
        case HK_OVERLAY:
            if (g_overlayCorner) { g_overlayLastCorner = g_overlayCorner; g_overlayCorner = 0; }
            else                 g_overlayCorner = g_overlayLastCorner ? g_overlayLastCorner : 1;
            SaveSettings();
            break;
        }
        return 0;
    }

    case WM_CLOSE:           // ✕: منخبّي البرنامج جنب الساعة (إلا إذا اخترت "إغلاق البرنامج")
        if (g_closeToTray && !g_reallyQuit && g_tray.IsAdded())
        {
            HideToTray();
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_ENDSESSION:      // ويندوز عم يطفي: منسكّر صح (وبينحفظ التسجيل)
        if (wParam)
            QuitApp();
        return 0;

    case WM_SHOWAPP:         // حدا فتح البرنامج مرة تانية: منبيّن هالنسخة بدل نسخة جديدة
        ShowFromTray();
        return 0;

    case WM_TRAY:
        // (الطريقة الحديثة: كبسة = NIN_SELECT ، كليك يمين = WM_CONTEXTMENU. ويندوز بيبعت كمان
        //  WM_LBUTTONUP و WM_RBUTTONUP بس ما منستعملهم لحتى ما ينعمل الشي مرتين)
        switch (LOWORD(lParam))
        {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            if (IsWindowVisible(hwnd) && !IsIconic(hwnd) && GetForegroundWindow() == hwnd)
                HideToTray();          // كبسة تانية بتخبّيه
            else
                ShowFromTray();
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu(POINT{ GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) });
            break;
        }
        return 0;

    case WM_STREAMER_DONE:   // البث (أو الإعادة) وقف لحالو (مشكلة بمشفّر الفيديو)
        if (lParam == 1)
        {
            g_replay.Stop();
            ShowMessage(L"توقّفت الإعادة: حدث خطأ في مشفّر الفيديو", true);
        }
        else
        {
            StopStreaming(L"");
            ShowMessage(L"توقّف البث: حدث خطأ في مشفّر الفيديو", true);
        }
        return 0;

    case WM_RECORDER_DONE:   // المسجّل وقف لحالو (صار خطأ)
    {
        const HRESULT reason = (HRESULT)wParam;
        const HRESULT hr     = g_recorder.Stop();
        FinishRecording(FAILED(reason) ? reason : hr);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_FRAME);
        SaveScenes();
        g_hotkeys.UnregisterAll();
        g_streamer.Stop();
        g_replay.Stop();
        g_recorder.Stop();           // إذا سكّرت وهو عم يسجّل، منحفظ الملف أول
        g_systemAudio.Stop();
        g_micAudio.Stop();
        g_appMixer.Stop();
        g_overlay.Destroy();
        g_tray.Remove();
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_TASKBARCREATED && WM_TASKBARCREATED)     // شريط المهام رجع اشتغل: منرجّع الأيقونة
    {
        g_tray.Add(hwnd, g_icons[g_iconState == 3 ? 1 : std::max(0, g_iconState)][1], L"مسجّل الشاشة");
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// =============================================================
//  نقطة البداية
// =============================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR cmdLine, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"ScreenRecorderWindow";

    // نسخة وحدة بس: إذا البرنامج شغّال (حتى لو مخبّى جنب الساعة) منبيّنه ومنطلع
    // (منفحص أول شي، قبل ما نفتح الصوت والكاميرا)
    HANDLE single = CreateMutexW(nullptr, TRUE, L"ScreenRecorder_SingleInstance");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND other = FindWindowW(CLASS_NAME, nullptr))
        {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(other, WM_SHOWAPP, 0, 0);
        }
        CloseHandle(single);
        return 0;
    }
    // --tray: بدأ مع ويندوز، فمنبلّش مخفي جنب الساعة
    const bool startHidden = cmdLine && wcsstr(cmdLine, L"--tray") != nullptr;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2d.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)g_dwrite.GetAddressOf());
    CreateTextFormats();


    // مجلد الحفظ: Videos\Screen Recorder
    PWSTR videos = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &videos)))
    {
        g_folder = std::wstring(videos) + L"\\Screen Recorder";
        CoTaskMemFree(videos);
    }
    else
    {
        g_folder = L"C:\\Screen Recorder";
    }

    LoadSettings();
    g_encoders = Recorder::ListEncoders();

    if (g_systemOn) g_systemAudio.Start();
    if (g_micOn)    g_micAudio.Start();

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.style         = CS_DBLCLKS;          // لحتى يوصلنا الدبل كليك
    RegisterClassW(&wc);

    WNDCLASSW ic = {};
    ic.lpfnWndProc   = InputProc;
    ic.hInstance     = hInstance;
    ic.lpszClassName = INPUT_CLASS;
    ic.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    ic.style         = CS_DROPSHADOW;
    RegisterClassW(&ic);


    // نوع نافذة القوائم المنبثقة (مع ظل)
    WNDCLASSW mc = {};
    mc.lpfnWndProc   = MenuProc;
    mc.hInstance     = hInstance;
    mc.lpszClassName = MENU_CLASS;
    mc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    mc.style         = CS_DROPSHADOW;
    RegisterClassW(&mc);

    const UINT dpi = GetDpiForSystem();
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"مسجّل الشاشة", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1200, dpi, 96), MulDiv(800, dpi, 96),
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd)
        return 0;

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    ShowWindow(hwnd, startHidden ? SW_HIDE : nCmdShow);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // تنظيف
    for (auto& state : g_icons)
        for (HICON icon : state)
            if (icon) DestroyIcon(icon);
    DiscardRenderTarget();
    g_fTitle.Reset(); g_fSection.Reset(); g_fBody.Reset(); g_fSmall.Reset(); g_fSmallEnd.Reset(); g_fSmallLtr.Reset();
    g_fCenter.Reset(); g_fSegment.Reset(); g_fTimer.Reset(); g_fIcon.Reset(); g_fIconBig.Reset();
    g_dwrite.Reset();
    g_d2d.Reset();

    MFShutdown();
    CoUninitialize();
    return 0;
}
