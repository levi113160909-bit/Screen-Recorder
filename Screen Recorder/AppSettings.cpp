// =============================================================
//  AppSettings.cpp - الإعدادات: الإطارات ومعدّل البت والمشفّر، وحفظ وتحميل ملف الإعدادات، والتشغيل مع ويندوز
// =============================================================
#include "App.h"

UINT32       g_customMbps     = 12;

UINT32 g_monitorHz = 0;      // تردد الشاشة اللي عم تتسجّل (بيتحدّث مع AutoFps)

// -------------------------------------------------------------
//  التشغيل مع ويندوز (مفتاح Run بالريجستري، مع --tray لحتى يبدأ مخفي)
// -------------------------------------------------------------
const wchar_t RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

const wchar_t RUN_NAME[] = L"ScreenRecorder";

// بتنستعمل قبل تعريفها بهالملف
UINT32 AutoFps();

UINT32 MonitorHz() { AutoFps(); return g_monitorHz; }

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
    g_monitorHz = hz;
    cached = hz >= 59 ? 60 : std::max<UINT32>(24, hz);   // أقصى شي 60
    return cached;
}

UINT32 CurrentFps()
{
    return g_fpsChoice == 0 ? AutoFps() : FPS_OPTIONS[g_fpsChoice];
}

// إطارات البث (المنصات أقصى شي 60)
UINT32 StreamFps()
{
    if (g_streamFpsChoice == 1) return 30;
    if (g_streamFpsChoice == 2) return 60;
    return std::min<UINT32>(CurrentFps(), 60);
}

// خطوة +/- لمعدّل بت البث
void ChangeStreamKbps(int direction)
{
    const UINT32 step = g_streamKbps > 10000 || (direction > 0 && g_streamKbps >= 10000) ? 1000 : 500;
    if (direction < 0) g_streamKbps = std::max<UINT32>(1000, g_streamKbps - step);
    else               g_streamKbps = std::min<UINT32>(STREAM_KBPS_MAX, g_streamKbps + step);
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
    put(L"FpsChoice2",  std::to_wstring(g_fpsChoice));
    put(L"Quality",     std::to_wstring(g_quality));
    put(L"CustomMbps",  std::to_wstring(g_customMbps));
    put(L"EncoderMode", std::to_wstring(g_encoderMode));
    put(L"EncoderName", g_encoderName);
    put(L"CrashSafe",   g_crashSafe ? L"1" : L"0");
    put(L"SeparateTracks", g_separateTracks ? L"1" : L"0");
    put(L"Codec",          std::to_wstring(g_codec));
    put(L"OverlayCorner",  std::to_wstring(g_overlayCorner));
    put(L"CloseToTray",    g_closeToTray ? L"1" : L"0");
    put(L"Theme",          std::to_wstring(g_themeChoice));
    put(L"AutoGame",       g_autoGame ? L"1" : L"0");
    put(L"FirstRunDone",   g_firstRunDone ? L"1" : L"0");
    put(L"Accent",         std::to_wstring(g_accentChoice));
    put(L"AccentCustom",   std::to_wstring(g_accentCustom));
    put(L"UpdateCheckDay", std::to_wstring(g_updateCheckDay));
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
    put(L"StreamFps",    std::to_wstring(g_streamFpsChoice));
    put(L"StreamSaveCopy", g_streamSaveCopy ? L"1" : L"0");
    put(L"StreamAdaptive", g_streamAdaptive ? L"1" : L"0");
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
    // الإعداد الجديد، وإذا ما في منحوّل القديم (تلقائي، 24، 30، 60، 120، متغيّر)
    if (getInt(L"FpsChoice2", -1) >= 0)
        g_fpsChoice = std::clamp(getInt(L"FpsChoice2", 0), 0, FPS_VARIABLE);
    else
    {
        const int oldMap[] = { 0, 1, 1, 2, 5, FPS_VARIABLE };
        g_fpsChoice = oldMap[std::clamp(getInt(L"FpsChoice", 0), 0, 5)];
    }
    g_quality      = std::clamp(getInt(L"Quality", 2), 0, 3);
    g_customMbps   = (UINT32)std::clamp(getInt(L"CustomMbps", 12), 1, 150);
    g_encoderMode  = std::clamp(getInt(L"EncoderMode", 0), 0, 2);
    g_encoderName  = getStr(L"EncoderName");
    g_crashSafe    = getInt(L"CrashSafe", 1) != 0;
    g_separateTracks = getInt(L"SeparateTracks", 0) != 0;
    g_codec          = std::clamp(getInt(L"Codec", 0), 0, 2);
    g_overlayCorner  = std::clamp(getInt(L"OverlayCorner", 1), 0, 4);
    g_closeToTray    = getInt(L"CloseToTray", 1) != 0;
    g_themeChoice    = std::clamp(getInt(L"Theme", 0), 0, Theme::THEME_COUNT - 1);
    g_autoGame       = getInt(L"AutoGame", 1) != 0;
    g_firstRunDone   = getInt(L"FirstRunDone", 0) != 0;
    g_accentChoice   = std::clamp(getInt(L"Accent", 0), 0, Theme::ACCENT_COUNT);
    g_accentCustom   = (UINT32)getInt(L"AccentCustom", 0x4F8CFF) & 0xFFFFFF;
    g_updateCheckDay = getInt(L"UpdateCheckDay", 0);
    Theme::Apply(g_themeChoice, g_accentChoice, g_accentCustom);
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
    g_streamKbps = (UINT32)std::clamp(getInt(L"StreamKbps", 6000), 1000, (int)STREAM_KBPS_MAX);
    g_streamFpsChoice = std::clamp(getInt(L"StreamFps", 0), 0, 2);
    g_streamSaveCopy  = getInt(L"StreamSaveCopy", 1) != 0;
    g_streamAdaptive  = getInt(L"StreamAdaptive", 1) != 0;
    g_verticalMode     = std::clamp(getInt(L"VerticalMode", 0), 0, 1);
    g_replaySet.seconds  = std::clamp(getInt(L"ReplayChoice", 1), 0, 3);
    g_replayWanted       = getInt(L"ReplayOn", 0) != 0;
    g_replaySet.source   = std::clamp(getInt(L"ReplaySource", 0), 0, 1);
    g_replaySet.monitor  = std::clamp(getInt(L"ReplayMonitor", 0), 0, 3);
    g_replaySet.res      = std::clamp(getInt(L"ReplayRes", 1), 0, 2);
    g_replaySet.fps      = std::clamp(getInt(L"ReplayFps", 1), 0, 1);
    g_replaySet.quality  = std::clamp(getInt(L"ReplayQuality", 1), 0, 3);
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
