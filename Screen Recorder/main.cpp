// =============================================================
//  main.cpp - نقطة البداية: إنشاء النافذة، ومعالجة رسائل ويندوز (الماوس، الكيبورد، المؤقّت...)
// =============================================================
#include "App.h"

bool                   g_ignoreNextUp = false;   // بعد الدبل كليك ما منعتبر الكبسة مرتين

D2D1_RECT_F              g_appVolTrack{};

int          g_overlayLastCorner = 1;  // لما تخفيه بالاختصار ومنرجّعه، بيرجع لنفس الزاوية

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
        SyncStartupMode();          // التشغيل مع ويندوز: مفتاح Run أو جدولة المهام (حسب وضع المسؤول)
        if (g_elevationDeclined)
            Notify(Glyph::Warning, Theme::Yellow, L"يعمل البرنامج بصلاحية عادية",
                   L"لم تتم الموافقة على صلاحية المسؤول، فقد لا تعمل الاختصارات داخل بعض الألعاب");
        {
            // البرنامج وقع بالتشغيل الماضي؟ منقلّك وين التقرير
            std::wstring report;
            if (TakePendingCrash(report))
            {
                Notify(Glyph::Warning, Theme::Yellow, L"توقّف البرنامج بشكل مفاجئ في المرة السابقة",
                       L"حُفظ تقرير بالخطأ: الإعدادات ← عام ← تقارير الأعطال");
                ShowMessage(L"حُفظ تقرير عن العطل السابق في مجلد تقارير الأعطال", true);
            }
        }
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
            os.uploadKbps = 0;
            if (os.streaming)
                for (const OutputStatus& st : g_streamStatus) os.uploadKbps += st.kbps;
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
        if (g_tick == 12 && !g_firstRunDone && IsWindowVisible(hwnd) && !g_panels.IsOpen())   // أول مرة: الإعداد السريع
            ShowFirstRun();
        if (g_tick == 20 && g_replaySet.autoStart && !g_replay.IsActive())   // تشغيل تلقائي (من إعدادات الإعادة)
            StartReplay(true);
        if (g_streamer.IsActive())
            g_streamStatus = g_streamer.Status();
        if (g_tick % 150 == 75)            // كل 5 ثواني تقريباً
            CheckDiskSpace();
        if (g_tick % 60 == 30 && g_tick > 90)   // كل ثانيتين: في لعبة؟
            CheckGame(hwnd);
        if (g_tick == 300)                 // بعد 10 ثواني من الفتح: في تحديث؟ (مرة باليوم)
        {
            SYSTEMTIME st; GetLocalTime(&st);
            const int today = st.wYear * 1000 + st.wMonth * 40 + st.wDay;
            if (today != g_updateCheckDay && !g_updateChecking)
            {
                g_updateChecking = true;
                CheckForUpdate(hwnd, APP_REPO, APP_VERSION, false);
            }
        }
        // اضغط لتتكلم: مفعّل (إله زر) ← المايك مكتوم إلا وانت ضاغط
        {
            const bool ptt = g_hotkeys.Get(HK_PUSH_TO_TALK).vk != 0;
            if (ptt && g_pttReleaseTick && g_tick >= g_pttReleaseTick)
            {
                g_pttReleaseTick = 0;
                g_micAudio.SetMuted(true);
            }
            if (ptt && !g_pttWasOn)                 // انفعّل هلق: منبلّش مكتوم
                g_micAudio.SetMuted(true);
            if (!ptt && g_pttWasOn)                 // انلغى: منرجّع المايك
                g_micAudio.SetMuted(false);
            g_pttWasOn = ptt;
        }
        if (g_tick % 30 == 0 && (g_recorder.IsRecording() || g_streamer.IsActive()))   // وقت التسجيل بتلميح الأيقونة
            g_tray.SetTip((g_recorder.IsRecording() ? L"● جارٍ التسجيل  " + FormatTime(g_recorder.ElapsedMs())
                                                    : L"● بث مباشر  " + FormatTime(g_streamer.ElapsedMs())) + L"\nمسجّل الشاشة");
        if (g_tick % 60 == 0)           // كل ثانيتين: النوافذ والبرامج اللي رجعت انفتحت
        {
            // لعبة شغّالة كمسؤول قدامك والبرنامج عادي؟ منقلّك (مرة وحدة) إنو الاختصارات ممكن ما تشتغل جوّاها
            static bool elevatedTipShown = false;
            if (!elevatedTipShown && !IsElevated() && g_gameHwnd && GetForegroundWindow() == g_gameHwnd &&
                WindowIsMoreElevated(g_gameHwnd))
            {
                elevatedTipShown = true;
                Notify(Glyph::Game, Theme::Yellow, L"اللعبة تعمل كمسؤول",
                       L"إذا لم تعمل الاختصارات: الإعدادات ← عام ← الصلاحية ← كمسؤول");
            }
            ReconnectLostWindows();
            g_appMixer.Reconnect();
            FollowWindowAspect();
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
        mm->ptMinTrackSize.x = MulDiv(1030, g_dpi, 96);   // زر المعرض صار فيه كلمة: لحتى ما يلمس زر الإعادة زر البث
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
        if (IDB_APP_VOL.Has(hit))   // شريط علو برنامج
        {
            const int i = IDB_APP_VOL.Index(hit);
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
        if (IDB_SCENE.Has(hit))
        {
            g_ignoreNextUp = true;
            RenameScene(IDB_SCENE.Index(hit));
            return 0;
        }
        if (IDB_APP_VOL.Has(hit))  // دبل كليك على شريط برنامج = 100%
        {
            g_appMixer.SetVolume(IDB_APP_VOL.Index(hit), 1.0f);
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
        if (IDB_SCENE.Has(hit))
        {
            ShowSceneMenu(IDB_SCENE.Index(hit), DipToScreen(p.x, p.y));
            return 0;
        }
        if (IDB_LAYER.Has(hit)) id = IDB_LAYER.Index(hit);
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

    case WM_STREAM_COPY_SAVED:    // انحفظت نسخة البث
    {
        std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
        if (path)
            Notify(Glyph::Check, Theme::Green, L"حُفظت نسخة من البث", FileNameOf(*path));
        return 0;
    }

    case WM_UPDATE_RESULT:        // نتيجة فحص التحديثات
    {
        std::unique_ptr<UpdateInfo> info(reinterpret_cast<UpdateInfo*>(lParam));
        const bool manual = wParam != 0;
        g_updateChecking = false;
        if (info && info->ok)
        {
            SYSTEMTIME st; GetLocalTime(&st);
            g_updateCheckDay = st.wYear * 1000 + st.wMonth * 40 + st.wDay;
            g_update = *info;
            SaveSettings();
            if (info->newer)
                Notify(Glyph::Check, Theme::Accent, L"يتوفر إصدار جديد " + info->version, L"من الإعدادات ← عام ← حول البرنامج");
            else if (manual)
                Notify(Glyph::Check, Theme::Green, L"لديك أحدث إصدار", L"الإصدار " + std::wstring(APP_VERSION));
        }
        else if (manual)
            Notify(Glyph::Warning, Theme::Yellow, L"تعذّر التحقق من التحديثات", L"تحقّق من الاتصال بالإنترنت");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_VCAM_RESULT:
    {
        const std::wstring err = VirtualCamTakeError(lParam);
        if (wParam == 1)
        {
            Notify(Glyph::Camera, Theme::Green, L"الكاميرا الافتراضية تعمل", L"اختر «مسجّل الشاشة» ككاميرا في ديسكورد أو زوم أو تيمز");
            ShowMessage(L"✓ الكاميرا الافتراضية تعمل", false);
        }
        else if (wParam == 0)
        {
            Notify(Glyph::Warning, Theme::Red, L"تعذّر تشغيل الكاميرا الافتراضية", err);
            ShowMessage(err.empty() ? std::wstring(L"تعذّر تشغيل الكاميرا الافتراضية") : err, true);
        }
        else
            ShowMessage(L"أُوقفت الكاميرا الافتراضية", false);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_SCREENSHOT_DONE:
    {
        const std::wstring path = FinishScreenshot(hwnd, wParam, lParam);
        if (path.empty())
            Notify(Glyph::Warning, Theme::Red, L"تعذّر حفظ لقطة الشاشة");
        else
        {
            Notify(Glyph::Camera, Theme::Green, L"حُفظت لقطة الشاشة", L"ونُسخت أيضًا (الصقها بـ Ctrl+V)");
            ShowMessage(L"✓ لقطة شاشة: " + FileNameOf(path), false);
        }
        return 0;
    }

    case WM_PUSH_TO_TALK:         // اضغط لتتكلم: المايك بيفتح وانت ضاغط ، وبيسكّر بعد ما تترك بشوي
        if (wParam)
        {
            g_pttReleaseTick = 0;
            g_micAudio.SetMuted(false);
        }
        else
            g_pttReleaseTick = g_tick + 8;          // ربع ثانية تقريباً: لحتى ما تنقص آخر كلمة
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_HOTKEY_CAPTURE:       // نافذة الاختصارات: انمسك زر جديد
        g_hotkeys.OnCaptureMessage(wParam, lParam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_HOTKEY:
    {
        const int slot = HotkeyManager::SlotFromId(wParam);
        {
            // نفس الإجراء ممكن يوصل من طريقين (ويندوز + مراقب الألعاب اللي شغّالة كمسؤول): منتجاهل التكرار
            static std::vector<ULONGLONG> last;
            if (slot < 0)
                return 0;
            if ((int)last.size() <= slot)
                last.resize(slot + 1, 0);
            const ULONGLONG now = GetTickCount64();
            if (now - last[slot] < 300)
                return 0;
            last[slot] = now;
        }
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
        case HK_SCREENSHOT:
            TakeScreenshot(g_scene, g_folder, hwnd);
            break;
        case HK_MARKER:
            AddMarker();
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

    case WM_TEST_CRASH:      // --test-crash: منوقع عن قصد لنجرّب سجل الأعطال
        TestCrash();

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
        VirtualCamShutdown();        // منشيل الكاميرا الافتراضية من ويندوز
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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR cmdLine, int nCmdShow)
{
    InstallCrashHandler(APP_VERSION);      // أول شي: أي عطل بعد هون بينكتب تقرير (CrashLog.h)
    const wchar_t CLASS_NAME[] = L"ScreenRecorderWindow";
    // نسخة "كمسؤول" فتحتها النسخة العادية (Elevation.h): بتستنى العادية تسكّر بدل ما تطلع
    const bool elevatedRestart = cmdLine && wcsstr(cmdLine, L"--elevated-restart") != nullptr;

    // نسخة وحدة بس: إذا البرنامج شغّال (حتى لو مخبّى جنب الساعة) منبيّنه ومنطلع
    // (منفحص أول شي، قبل ما نفتح الصوت والكاميرا)
    HANDLE single = CreateMutexW(nullptr, TRUE, L"ScreenRecorder_SingleInstance");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS && elevatedRestart &&
        WaitForSingleObject(single, 20000) != WAIT_TIMEOUT)
    {
        // النسخة القديمة سكّرت ، وصرنا نحنا النسخة الوحيدة
    }
    else if (single && GetLastError() == ERROR_ALREADY_EXISTS)
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

    // وضع المسؤول (الإعدادات ← عام ← الصلاحية): منعيد فتح البرنامج كمسؤول قبل ما نفتح الصوت والنافذة
    if (g_runAsAdmin && !IsElevated() && !elevatedRestart)
    {
        if (RelaunchElevated(startHidden ? L"--elevated-restart --tray" : L"--elevated-restart"))
        {
            ReleaseMutex(single);
            CloseHandle(single);
            MFShutdown();
            CoUninitialize();
            return 0;
        }
        g_elevationDeclined = true;          // رفضت الموافقة: منكمّل بصلاحية عادية
    }
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
    if (IsElevated())
    {
        // كمسؤول: ويندوز بيمنع البرامج العادية تبعتلنا رسائل. منسمح بهدول بس:
        // أيقونة الساعة (من explorer) ، رجعة شريط المهام ، و"افتح البرنامج" من نسخة تانية
        AllowMessageFromLowerPrivilege(hwnd, WM_TRAY);
        AllowMessageFromLowerPrivilege(hwnd, WM_TASKBARCREATED);
        AllowMessageFromLowerPrivilege(hwnd, WM_SHOWAPP);
    }
    ShowWindow(hwnd, startHidden ? SW_HIDE : nCmdShow);
    if (cmdLine && wcsstr(cmdLine, L"--test-crash"))     // تجربة سجل الأعطال
        PostMessageW(hwnd, WM_TEST_CRASH, 0, 0);

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
