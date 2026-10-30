// =============================================================
//  Commands.cpp - تنفيذ الكبسات: كل زر بالواجهة الرئيسية بيوصل لهون برقمه (شوف AppIds.h)
// =============================================================
#include "App.h"

const wchar_t* APP_URL        = L"https://github.com/levi113160909-bit/Screen-Recorder";

void OnClick(int id, D2D1_POINT_2F p)
{
    const bool rec = Busy();
    const POINT screen = DipToScreen(p.x, p.y);

    // أصوات البرامج
    if (id >= IDB_APP_ROW.first && id < IDB_APP_VOL.End())
    {
        if (IDB_APP_VOL.Has(id))
            return;                                   // الشريط بينسحب (مش كبسة)
        if (IDB_APP_DELETE.Has(id))
        {
            g_appMixer.Remove(IDB_APP_DELETE.Index(id));
            g_appLevels.clear();
            SaveScenes();
        }
        else if (IDB_APP_MUTE.Has(id))
        {
            const int i = IDB_APP_MUTE.Index(id);
            g_appMixer.SetMuted(i, !g_appMixer.Entry(i).muted);
            SaveScenes();
        }
        return;
    }

    // القوائم (كل كتلة إلها مجال محدد ، شوف AppIds.h)
    if (IDB_TARGET_MENU.Has(id))   { ShowTargetMenu(IDB_TARGET_MENU.Index(id), screen); return; }
    if (IDB_TARGET_TOGGLE.Has(id)) { ToggleTarget(IDB_TARGET_TOGGLE.Index(id)); return; }
    if (IDB_SCENE.Has(id))         { SwitchScene(IDB_SCENE.Index(id)); return; }
    if (IDB_LAYER_DELETE.Has(id))  { RemoveLayer(IDB_LAYER_DELETE.Index(id)); return; }
    if (IDB_LAYER_EYE.Has(id))     { LayerCommand(IDB_LAYER_EYE.Index(id), 8); return; }
    if (IDB_LAYER.Has(id))         { g_selectedLayer = IDB_LAYER.Index(id); return; }

    if (id >= ID_CANVAS_FIRST && id < ID_CANVAS_FIRST + 4)
    {
        if (!rec) { g_canvasChoice = id - ID_CANVAS_FIRST; ApplyCanvasChoice(); SaveSettings(); RestartReplayIfActive(); }
        return;
    }
    if (id >= ID_FPS_FIRST && id < ID_FPS_FIRST + 6)            // 60 .. 165
    {
        if (!rec) { g_fpsChoice = FPS_FIXED_FIRST + (id - ID_FPS_FIRST); SaveSettings(); RestartReplayIfActive(); }
        return;
    }
    if (id >= ID_CODEC_FIRST && id < ID_CODEC_FIRST + 3)
    {
        if (!rec) { g_codec = id - ID_CODEC_FIRST; SaveSettings(); }
        return;
    }
    if (id >= ID_FPS_MODE_FIRST && id < ID_FPS_MODE_FIRST + 3)  // تلقائي ، 30 ، مثل اللعبة
    {
        const int modes[] = { 0, 1, FPS_VARIABLE };
        if (!rec) { g_fpsChoice = modes[id - ID_FPS_MODE_FIRST]; SaveSettings(); RestartReplayIfActive(); }
        return;
    }
    if (id >= ID_ADMIN_FIRST && id < ID_ADMIN_FIRST + 2)
    {
        const bool want = (id == ID_ADMIN_FIRST + 1);
        if (want == g_runAsAdmin)
            return;
        if (Busy() || g_streamer.IsActive())
        {
            ShowMessage(L"أوقف التسجيل والبث أولًا", true);
            return;
        }
        const bool startup = IsRunAtStartup();
        g_runAsAdmin = want;
        SaveSettings();
        if (want && !IsElevated())
        {
            // منعيد فتح البرنامج كمسؤول (ويندوز بيسأل موافقة) ، والنسخة الجديدة بتكمّل من نفس المكان
            if (RelaunchElevated(L"--elevated-restart"))
            {
                g_reallyQuit = true;
                DestroyWindow(g_hwnd);       // بيحفظ كلشي ، والنسخة الجديدة ناطرة نسكّر
                return;
            }
            g_runAsAdmin = false;            // رفضت الموافقة: منرجع متل ما كنا
            SaveSettings();
            ShowMessage(L"لم تتم الموافقة على صلاحية المسؤول", true);
            return;
        }
        // تبديل طريقة التشغيل مع ويندوز للطريقة المناسبة
        if (startup)
            SetRunAtStartup(true);
        if (!want && IsElevated())
            ShowMessage(L"سيعمل البرنامج بصلاحية عادية من المرة القادمة التي تفتحه فيها", false);
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
    if (id == ID_SETUP_AGAIN)
    {
        if (!Busy()) ShowFirstRun();
        return;
    }
    if (id >= ID_AUTOGAME_FIRST && id < ID_AUTOGAME_FIRST + 2)
    {
        g_autoGame = (id == ID_AUTOGAME_FIRST);
        if (!g_autoGame && g_gameHwnd)
        {
            g_gameHwnd = nullptr;
            if (g_replayByGame) StopReplayQuiet();
            else if (g_replay.IsActive() && g_replaySet.source == 0) RestartReplayQuiet();
            g_replayByGame = false;
        }
        SaveSettings();
        return;
    }
    if (id >= ID_THEME_FIRST && id < ID_THEME_FIRST + Theme::THEME_COUNT)
    {
        g_themeChoice = id - ID_THEME_FIRST;
        Theme::Apply(g_themeChoice, g_accentChoice, g_accentCustom);
        SaveSettings();
        return;
    }
    if (id == ID_ACCENT_FIRST + Theme::ACCENT_COUNT)      // مخصّص: دائرة الألوان
    {
        const UINT32 start = g_accentChoice == Theme::ACCENT_COUNT ? g_accentCustom : 0x4F8CFF;
        const int prevChoice = g_accentChoice;
        g_panels.Open(std::make_unique<ColorPickerPanel>(start,
            [](UINT32 c) { Theme::Apply(g_themeChoice, Theme::ACCENT_COUNT, c); InvalidateRect(g_hwnd, nullptr, FALSE); },
            [prevChoice](UINT32 c, bool ok)
            {
                if (ok) { g_accentChoice = Theme::ACCENT_COUNT; g_accentCustom = c; SaveSettings(); }
                else    g_accentChoice = prevChoice;
                Theme::Apply(g_themeChoice, g_accentChoice, g_accentCustom);
                OpenSettings(SP_GENERAL);
            }));
        return;
    }
    if (id >= ID_ACCENT_FIRST && id < ID_ACCENT_FIRST + Theme::ACCENT_COUNT)
    {
        g_accentChoice = id - ID_ACCENT_FIRST;
        Theme::Apply(g_themeChoice, g_accentChoice, g_accentCustom);
        SaveSettings();
        return;
    }
    if (id == ID_ABOUT_GITHUB)
    {
        ShellExecuteW(nullptr, L"open", APP_URL, nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (id == ID_CHECK_UPDATE)
    {
        if (!g_updateChecking)
        {
            g_updateChecking = true;
            CheckForUpdate(g_hwnd, APP_REPO, APP_VERSION, true);
        }
        return;
    }
    if (id == ID_GET_UPDATE)
    {
        ShellExecuteW(nullptr, L"open", g_update.url.empty() ? APP_URL : g_update.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (id == ID_OPEN_CRASHES)
    {
        const std::wstring dir = CrashFolder();
        CreateDirectoryW(dir.c_str(), nullptr);        // لحتى ينفتح حتى لو ما صار ولا عطل
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (id == ID_OPEN_CONFIG)
    {
        const std::wstring path = ScenesPath();
        const size_t slash = path.find_last_of(L'\\');
        if (slash != std::wstring::npos)
            ShellExecuteW(nullptr, L"open", path.substr(0, slash).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
    if (id >= ID_STREAM_COPY_FIRST && id < ID_STREAM_COPY_FIRST + 2)
    {
        if (!g_streamer.IsActive()) { g_streamSaveCopy = (id == ID_STREAM_COPY_FIRST); SaveSettings(); }
        return;
    }
    if (id >= ID_STREAM_ADAPT_FIRST && id < ID_STREAM_ADAPT_FIRST + 2)
    {
        if (!g_streamer.IsActive()) { g_streamAdaptive = (id == ID_STREAM_ADAPT_FIRST); SaveSettings(); }
        return;
    }
    if (id >= ID_STREAM_FPS_FIRST && id < ID_STREAM_FPS_FIRST + 3)
    {
        if (!g_streamer.IsActive()) { g_streamFpsChoice = id - ID_STREAM_FPS_FIRST; SaveSettings(); }
        return;
    }
    if (id >= ID_STREAM_PRESET_FIRST && id < ID_STREAM_PRESET_FIRST + 6)
    {
        if (!g_streamer.IsActive()) { g_streamKbps = STREAM_PRESETS[id - ID_STREAM_PRESET_FIRST]; SaveSettings(); }
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
    case ID_STREAM_SETTINGS: OpenSettings(SP_STREAM); break;
    case ID_VCAM_TOGGLE:     ToggleVirtualCam(); break;
    case ID_ADD_TARGET:    AddTarget(screen); break;
    case ID_STREAM_KBPS_MINUS:
        if (!g_streamer.IsActive()) { ChangeStreamKbps(-1); SaveSettings(); }
        break;
    case ID_STREAM_KBPS_PLUS:
        if (!g_streamer.IsActive()) { ChangeStreamKbps(+1); SaveSettings(); }
        break;
    case ID_BITRATE_MINUS: if (!rec) { ChangeBitrate(-1); SaveSettings(); } break;
    case ID_BITRATE_PLUS:  if (!rec) { ChangeBitrate(+1); SaveSettings(); } break;
    case ID_ENCODER_MENU:  if (!rec) { ShowEncoderMenu(screen); RestartReplayIfActive(); } break;
    case ID_RECORD:        ToggleRecording(); break;
    case ID_PAUSE:         TogglePause();     break;
    case ID_FOLDER:        OpenGallery();     break;
    case ID_ADD_SOURCE:    ShowAddSourceMenu(screen); break;
    case ID_SYSTEM_AUDIO:  SetAudioEnabled(g_systemAudio, g_systemOn, !g_systemOn); break;
    case ID_MIC:           SetAudioEnabled(g_micAudio, g_micOn, !g_micOn); break;
    case ID_SYSTEM_DEVICE: ShowDeviceMenu(g_systemAudio, g_systemOn, AudioSource::Kind::System, screen); break;
    case ID_MIC_DEVICE:    ShowDeviceMenu(g_micAudio, g_micOn, AudioSource::Kind::Microphone, screen); break;
    }
}
