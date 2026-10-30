// =============================================================
//  SettingsPages.cpp - صفحات نافذة الإعدادات اللي مربوطة مباشرة بإعدادات البرنامج
// =============================================================
#include "App.h"

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
    case SP_RECORDING:
    {
        Locked();
        int pw, ph;
        PrimaryMonitorSize(pw, ph);
        Option(L"دقة الفيديو", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"الشاشة", L"1440p", L"1080p", L"720p" }, g_canvasChoice, O + ID_CANVAS_FIRST, !locked);
            y += 40;
            Hint((g_canvasChoice == 0 ? L"بحجم الشاشة الرئيسية ← " + SizeText(pw & ~1, ph & ~1)
                                      : SizeText(CANVAS_H[g_canvasChoice] * 16 / 9, CANVAS_H[g_canvasChoice]))
                 + L" • حجم الصورة التي تُسجَّل");
        });
        Option(L"معدّل الإطارات", [&]
        {
            // سطر الأرقام (متل قائمة تردد الشاشة بويندوز)، وتحته الأوضاع
            const int fixedSel = (g_fpsChoice >= FPS_FIXED_FIRST && g_fpsChoice < FPS_VARIABLE) ? g_fpsChoice - FPS_FIXED_FIRST : -1;
            DrawSegmented(Rect(l, y, r, y + 34), { L"60", L"75", L"100", L"120", L"144", L"165" }, fixedSel, O + ID_FPS_FIRST, !locked);
            y += 40;
            const int modeSel = g_fpsChoice == 0 ? 0 : (g_fpsChoice == 1 ? 1 : (g_fpsChoice == FPS_VARIABLE ? 2 : -1));
            DrawSegmented(Rect(l, y, r, y + 34), { L"تلقائي", L"30", L"مثل اللعبة" }, modeSel, O + ID_FPS_MODE_FIRST, !locked);
            y += 40;

            const UINT32 hz = MonitorHz();
            if (g_fpsChoice == FPS_VARIABLE)      // مثل اللعبة
            {
                Hint(L"يسجّل إطارًا فقط عندما تُظهر اللعبة إطارًا جديدًا — بلا تكرار ولا تقطيع", Theme::Green);
                Hint(L"الأفضل: أضف نافذة اللعبة نفسها كمصدر بدل الشاشة كاملة");
                Hint(L"برامج مثل Premiere قد لا تدعم هذا النوع؛ DaVinci وCapCut ويوتيوب تدعمه", Theme::Yellow);
            }
            else if (g_fpsChoice == 0)
                Hint(L"تلقائي: حسب تردد الشاشة (حتى 60) ← " + std::to_wstring(CurrentFps()) + L" إطار/ث");
            else
            {
                const UINT32 fps = FPS_OPTIONS[g_fpsChoice];
                if (hz > 0 && fps > hz + 1)
                    Hint(L"أعلى من تردد شاشتك (" + std::to_wstring(hz) + L" هرتز): ستتكرّر بعض الإطارات", Theme::Yellow);
                else
                    Hint(std::to_wstring(fps) + L" إطارًا ثابتًا في الثانية" + (hz ? L" • تردد شاشتك " + std::to_wstring(hz) + L" هرتز" : L""));
                if (fps > 60)
                {
                    Hint(L"أكثر من 60 للتسجيل فقط — البث والإعادة حتى 60", Theme::Yellow);
                    if (g_encoderMode == 2)
                        Hint(L"يُفضَّل مشفّر بطاقة الرسومات، فالمعالج قد لا يلحق", Theme::Yellow);
                    else
                        Hint(L"إذا كانت اللعبة لا تثبت على هذا الرقم، جرّب «مثل اللعبة»");
                }
            }
        });
        Option(L"الجودة ومعدّل البت", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"منخفضة", L"متوسطة", L"عالية", L"مخصّصة" }, g_quality, O + ID_QUALITY_FIRST, !locked);
            y += 42;
            const D2D1_RECT_F box   = Rect(l, y, r, y + 34);
            const D2D1_RECT_F plus  = Rect(l + 3, y + 3, l + 40, y + 31);     // + على اليسار (الأكبر لليسار)
            const D2D1_RECT_F minus = Rect(r - 40, y + 3, r - 3, y + 31);
            Fill(box, Theme::Background, 9);
            if (AddHit(plus, O + ID_BITRATE_PLUS) && !locked)   Fill(plus, Theme::Hover, 7);
            if (AddHit(minus, O + ID_BITRATE_MINUS) && !locked) Fill(minus, Theme::Hover, 7);
            Text(L"+", g_fCenter.Get(), plus,  locked ? Theme::Off : Theme::Text);
            Text(L"−", g_fCenter.Get(), minus, locked ? Theme::Off : Theme::Text);
            Text(MbpsText(CurrentBitrate()) + L" ميغابت/ث", g_fCenter.Get(), Rect(l + 44, y, r - 44, y + 34), Theme::Text);
            y += 40;
            Hint(L"الأعلى = صورة أوضح وملف أكبر");
        });
        Option(L"الترميز والمشفّر", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"H.264", L"HEVC (H.265)", L"AV1" }, g_codec, O + ID_CODEC_FIRST, !locked);
            y += 42;
            const D2D1_RECT_F enc = Rect(l, y, r, y + 34);
            Fill(enc, AddHit(enc, O + ID_ENCODER_MENU) && !locked ? Theme::Hover : Theme::Background, 9);
            Text(L"المشفّر: " + EncoderLabel() + L" (للتسجيل والبث والإعادة)", g_fBody.Get(), Rect(l + 34, y, r - 12, y + 34), locked ? Theme::SubText : Theme::Text);
            Text(Icon::Chevron, g_fIcon.Get(), Rect(l + 4, y, l + 30, y + 34), Theme::SubText);
            y += 42;
            static const wchar_t* hints[] = {
                L"H.264: الأكثر توافقًا، يعمل في كل البرامج والأجهزة",
                L"HEVC: أوضح بنفس الحجم • بعض برامج المونتاج القديمة لا تفتحه",
                L"AV1: الأحدث والأفضل • يحتاج RTX 40 أو RX 7000 أو Intel Arc" };
            Hint(hints[g_codec]);
            // الحالة: سطر واحد بس
            if (g_recorder.IsRecording() && g_recorder.UsingNvenc())
                Hint(L"يعمل الآن: مشفّر NVIDIA المباشر بأعلى إعدادات الجودة", Theme::Green);
            else if (g_recorder.IsRecording())
                Hint(L"يعمل الآن: " + g_recorder.EncoderName(), Theme::SubText);
            else if (g_encoderMode != 2)
                Hint(L"مع بطاقات NVIDIA يُستخدم مشفّرها مباشرةً بأعلى جودة، وإلا مشفّر ويندوز");
            if (!g_previewCompositor.UsingGpu())
                Hint(L"تنبيه: التصوير بالطريقة القديمة (GDI) لأن بطاقة الرسومات غير متاحة", Theme::Yellow);
        });
        Option(L"مكان حفظ التسجيلات", [&]
        {
            const D2D1_RECT_F box = Rect(l, y, r, y + 34);
            Fill(box, Theme::Background, 9);
            Text(LtrText(g_folder), g_fSmallEnd.Get(), Rect(l + 10, y, r - 10, y + 34), Theme::Text);
            y += 42;
            DrawButton(Rect(r - 130, y, r, y + 32), L"تغيير…", O + ID_FOLDER_CHANGE, false, !locked, Icon::Folder);
            DrawButton(Rect(r - 270, y, r - 140, y + 32), L"فتح المجلد", O + ID_FOLDER_OPEN, false, true);
            y += 40;
            const ULONGLONG freeB = FreeBytes(g_folder);
            if (freeB > 0)
            {
                bool low = false;
                const std::wstring t = DiskSummary(freeB, CurrentBitrate(), low);
                Hint(t, low ? Theme::Yellow : Theme::SubText);
            }
        });
        Option(L"صيغة الملف", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"MP4 آمن", L"MP4 عادي" }, g_crashSafe ? 0 : 1, O + ID_CONTAINER_FIRST, !locked);
            y += 40;
            Hint(g_crashSafe ? std::wstring(L"لا يضيع التسجيل إذا تعطّل البرنامج أو انقطعت الكهرباء")
                             : std::wstring(L"يضيع التسجيل كله إذا تعطّل البرنامج قبل الإيقاف"));
            if (g_crashSafe && g_codec != 0)
                Hint(L"الملف الآمن يعمل مع H.264 فقط؛ مع HEVC وAV1 يُحفظ كملف عادي", Theme::Yellow);
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
            Hint(L"بدء التسجيل وحفظه، حفظ الإعادة، لقطة الشاشة، البث، المايك... في زاوية المؤشر");
            Hint(L"تظهر فوق كل النوافذ والألعاب، على الشاشة التي تعمل عليها");
            Hint(L"الألعاب بوضع «ملء الشاشة الحصري» لا تسمح بظهور أي شيء فوقها: اختر «بلا حواف» (Borderless) في إعدادات اللعبة", Theme::Off);
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

    case SP_STREAM:
    {
        const bool live = g_streamer.IsActive();
        if (live)
        {
            Fill(Rect(L, y, R, y + 40), WithAlpha(Theme::Yellow, 0.12f), 10);
            Text(L"لا يمكن تغيير إعدادات البث أثناء البث", g_fBody.Get(), Rect(L + 16, y, R - 16, y + 40), Theme::Yellow);
            y += 50;
        }
        UINT32 sw, sh;
        StreamSize(sw, sh);
        const UINT32 sfps = StreamFps();
        Option(L"دقة البث", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"مثل التسجيل", L"1080p", L"720p" }, g_streamRes, O + ID_STREAM_RES_FIRST, !live);
            y += 40;
            Hint(L"يُبث بدقة " + SizeText((int)sw, (int)sh) + L"");
        });
        Option(L"إطارات البث", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"مثل التسجيل", L"30", L"60" }, g_streamFpsChoice, O + ID_STREAM_FPS_FIRST, !live);
            y += 40;
            Hint(std::to_wstring(sfps) + L" إطار/ث • 60 أنعم للألعاب لكنها تحتاج معدّل بت أعلى، و30 أفضل إذا كان الإنترنت ضعيفًا");
        });
        Option(L"معدّل البت للبث", [&]
        {
            int preset = -1;
            for (int i = 0; i < 6; ++i)
                if (STREAM_PRESETS[i] == g_streamKbps) preset = i;
            DrawSegmented(Rect(l, y, r, y + 34), { L"3000", L"4500", L"6000", L"8000", L"10000", L"12000" },
                          preset, O + ID_STREAM_PRESET_FIRST, !live);
            y += 42;
            // ضبط دقيق
            const D2D1_RECT_F box   = Rect(l, y, r, y + 34);
            const D2D1_RECT_F plus  = Rect(l + 3, y + 3, l + 40, y + 31);     // + على اليسار
            const D2D1_RECT_F minus = Rect(r - 40, y + 3, r - 3, y + 31);
            Fill(box, Theme::Background, 9);
            if (AddHit(plus, O + ID_STREAM_KBPS_PLUS) && !live)   Fill(plus, Theme::Hover, 7);
            if (AddHit(minus, O + ID_STREAM_KBPS_MINUS) && !live) Fill(minus, Theme::Hover, 7);
            Text(L"+", g_fCenter.Get(), plus,  live ? Theme::Off : Theme::Text);
            Text(L"−", g_fCenter.Get(), minus, live ? Theme::Off : Theme::Text);
            Text(std::to_wstring(g_streamKbps) + L" كيلوبت/ث", g_fCenter.Get(), Rect(l + 44, y, r - 44, y + 34), Theme::Text);
            y += 42;

            // النصيحة حسب الدقة والإطارات
            const bool hi = std::min(sw, sh) > 800;           // 1080p أو أكبر
            const wchar_t* rec = hi ? (sfps > 30 ? L"6000–9000" : L"4500–6000") : (sfps > 30 ? L"4500–6000" : L"3000–4500");
            Hint(std::wstring(L"المقترح لهذه الدقة والإطارات: ") + rec + L" • تويتش: 6000 كحد أقصى • يوتيوب يقبل أعلى");
            Hint(L"هذا هو الإعداد العام للمنصات، ويمكن تخصيص كل منصة من قائمتها (⋯ ← الترميز ومعدّل البت)");
            Hint(L"منفصل تمامًا عن معدّل بت التسجيل");
            if (g_streamer.IsActive() && g_streamer.UsingNvenc())
                Hint(L"🟢 مشفّر NVIDIA المباشر: معدّل ثابت (CBR) ونظر للأمام وجودة تكيّفية", Theme::Green);
            else if (g_encoderMode != 2)
                Hint(L"مع بطاقات NVIDIA يُستخدم مشفّرها مباشرةً بمعدّل ثابت (CBR) كما تطلب المنصات");
        });
        Option(L"مجموع استهلاك الرفع", [&]
        {
            UINT32 total = 0; int encodes = 0;
            UploadEstimate(total, encodes);
            if (g_targets.empty() || encodes == 0)
            {
                Hint(L"لا توجد منصات مفعّلة (أضفها من تبويب «البث»)");
                return;
            }
            Text(LtrText(std::to_wstring((total + 999) / 1000)) + L" ميغابت/ث", g_fCenter.Get(), Rect(l, y, r, y + 30), Theme::Text);
            y += 34;
            Hint(L"اترك هامشًا: سرعة الرفع عندك يجب أن تكون ≈ " + LtrText(std::to_wstring(total * 14 / 10 / 1000 + 1)) +
                 L" ميغابت/ث أو أكثر");
            Hint(L"بطاقة الرسومات تضغط " + std::to_wstring(encodes) + (encodes == 1 ? L" نسخة واحدة" : L" نسخ") +
                 L" • المنصات المتطابقة (نفس الاتجاه والترميز والمعدّل) تتشارك نسخة واحدة");
            for (int i = 0; i < (int)g_targets.size(); ++i)
            {
                const StreamTarget& t = g_targets[i];
                if (!t.enabled) continue;
                Hint(L"• " + t.name + L": " + (TargetHevc(t) ? L"HEVC" : L"H.264") + L" • " +
                     LtrText(std::to_wstring(TargetKbps(t))) + L" كيلوبت/ث" + (t.vertical ? L" • عمودي" : L""));
            }
        });
        Option(L"نسخة من البث على جهازك", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"احفظ نسخة", L"لا" }, g_streamSaveCopy ? 0 : 1, O + ID_STREAM_COPY_FIRST, !live);
            y += 40;
            Hint(g_streamSaveCopy ? std::wstring(L"تُحفظ في مجلد Streams داخل مجلد التسجيلات • من نفس الضغط: بلا أي حمل إضافي")
                                  : std::wstring(L"لن يُحفظ البث على جهازك"));
        });
        Option(L"معدّل البت الذكي", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"مفعّل", L"معطّل" }, g_streamAdaptive ? 0 : 1, O + ID_STREAM_ADAPT_FIRST, !live);
            y += 40;
            Hint(g_streamAdaptive ? std::wstring(L"إذا ضعف الإنترنت يُخفَّض المعدّل تلقائيًا بدل أن يتقطّع البث، ثم يعود عندما يتحسّن")
                                  : std::wstring(L"المعدّل ثابت دائمًا (قد يتقطّع البث إذا ضعف الإنترنت)"));
            if (g_encoderMode == 2)
                Hint(L"يعمل مع مشفّر NVIDIA المباشر", Theme::Yellow);
        });
        Option(L"البث العمودي (لتيك توك)", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"قصّ المنتصف", L"الشاشة كاملة" }, g_verticalMode, O + ID_VERTICAL_FIRST, !live);
            y += 40;
            Hint(g_verticalMode == 0 ? std::wstring(L"يُقصّ جزء من المنتصف بطول الشاشة: الصورة كبيرة وواضحة")
                                     : std::wstring(L"تظهر الشاشة كاملة في المنتصف وفوقها وتحتها أسود"));
        });
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
        Option(L"الصلاحية", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"عادية", L"كمسؤول" }, g_runAsAdmin ? 1 : 0, O + ID_ADMIN_FIRST, !Busy());
            y += 40;
            Hint(L"فعّل «كمسؤول» إذا لم تعمل الاختصارات داخل لعبة (كثير من ألعاب مضاد الغش تعمل كمسؤول)");
            Hint(L"يطلب ويندوز موافقتك عند فتح البرنامج، أما التشغيل مع ويندوز فيبقى بلا طلب");
            if (IsElevated())
                Hint(L"البرنامج يعمل الآن كمسؤول", Theme::Green);
            else if (g_runAsAdmin)
                Hint(L"البرنامج يعمل الآن بصلاحية عادية لأن طلب الموافقة رُفض", Theme::Yellow);
            else
                Hint(L"البرنامج يعمل الآن بصلاحية عادية", Theme::Off);
        });
        Option(L"الانتقال بين المشاهد", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"فوري", L"تلاشٍ سريع", L"تلاشٍ ناعم" }, g_transitionChoice, O + ID_TRANSITION_FIRST, true);
            y += 40;
        });
        Option(L"الألعاب", [&]
        {
            DrawSegmented(Rect(l, y, r, y + 34), { L"كشف تلقائي", L"معطّل" }, g_autoGame ? 0 : 1, O + ID_AUTOGAME_FIRST, true);
            y += 40;
            Hint(g_autoGame ? std::wstring(L"عند فتح لعبة تعمل الإعادة تلقائيًا وتلتقط نافذة اللعبة نفسها، وتتوقف عند إغلاقها")
                            : std::wstring(L"لن يتغيّر شيء عند فتح الألعاب"));
            if (g_autoGame && g_gameHwnd)
                Hint(L"اللعبة الحالية: " + g_gameTarget.name, Theme::Green);
        });
        Option(L"الإعداد السريع", [&]
        {
            DrawButton(Rect(r - 220, y, r, y + 34), L"إعادة الإعداد السريع", O + ID_SETUP_AGAIN, false, !locked);
            y += 42;
            Hint(L"اختر ما تريد فعله (لقطات / تسجيل / بث) ويضبط البرنامج كل شيء تلقائيًا");
        });
        Option(L"المظهر", [&]
        {
            std::vector<std::wstring> themes;
            for (int i = 0; i < Theme::THEME_COUNT; ++i) themes.push_back(Theme::ThemeName(i));
            DrawSegmented(Rect(l, y, r, y + 34), themes, g_themeChoice, O + ID_THEME_FIRST, true);
            y += 44;
            Text(L"اللون الأساسي", g_fSmall.Get(), Rect(l, y, r, y + 18), Theme::SubText);
            y += 24;
            // دوائر الألوان (من اليمين لليسار) + "مخصّص" بالآخر
            const float cell = (r - l) / (Theme::ACCENT_COUNT + 1);
            for (int i = 0; i <= Theme::ACCENT_COUNT; ++i)
            {
                const float cx = r - cell * i - cell / 2;
                const D2D1_RECT_F hit = Rect(cx - cell / 2 + 2, y, cx + cell / 2 - 2, y + 58);
                const bool hover = AddHit(hit, O + ID_ACCENT_FIRST + i);
                if (hover) Fill(hit, Theme::Hover, 8);
                if (i == Theme::ACCENT_COUNT)
                {
                    // دائرة ألوان صغيرة (6 قطع) أو اللون المخصّص إذا مختار
                    if (g_accentChoice == Theme::ACCENT_COUNT)
                        Circle(cx, y + 18, 13, D2D1::ColorF(g_accentCustom));
                    else
                        for (int k = 0; k < 6; ++k)
                        {
                            const float a = k * 3.14159f / 3;
                            Circle(cx + std::cos(a) * 6, y + 18 - std::sin(a) * 6, 6.5f, Theme::AccentPreview((k + 3) % 6));
                        }
                    if (i == g_accentChoice)
                    {
                        g_brush->SetColor(Theme::Text);
                        g_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y + 18), 17, 17), g_brush.Get(), 2);
                    }
                    Text(L"مخصّص", g_fSegment.Get(), Rect(hit.left, y + 36, hit.right, y + 56),
                         i == g_accentChoice ? Theme::Text : Theme::SubText);
                    continue;
                }
                Circle(cx, y + 18, 13, Theme::AccentPreview(i));
                if (i == g_accentChoice)
                {
                    g_brush->SetColor(Theme::Text);
                    g_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y + 18), 17, 17), g_brush.Get(), 2);
                }
                Text(Theme::AccentName(i), g_fSegment.Get(), Rect(hit.left, y + 36, hit.right, y + 56),
                     i == g_accentChoice ? Theme::Text : Theme::SubText);
            }
            y += 64;
        });
        Option(L"حول البرنامج", [&]
        {
            Circle(r - 18, y + 20, 16, Theme::Red);
            Circle(r - 18, y + 20, 6, Theme::White);
            Text(L"مسجّل الشاشة", g_fSection.Get(), Rect(l, y, r - 44, y + 22), Theme::Text);
            Text(L"الإصدار " + LtrText(APP_VERSION), g_fSmall.Get(), Rect(l, y + 22, r - 44, y + 40), Theme::SubText);
            y += 50;
            Hint(L"تسجيل وبث وإعادة بواجهة عربية، مجاني ومفتوح المصدر");
            Hint(L"يضغط الفيديو على بطاقة الرسومات مباشرةً (NVIDIA NVENC) لجودة عالية وحمل خفيف");
            y += 4;
            DrawButton(Rect(r - 170, y, r, y + 32), L"صفحة المشروع", O + ID_ABOUT_GITHUB, false, true);
            DrawButton(Rect(r - 350, y, r - 180, y + 32), L"مجلد الإعدادات", O + ID_OPEN_CONFIG, false, true, Icon::Folder);
            y += 40;
            DrawButton(Rect(r - 170, y, r, y + 32), L"تقارير الأعطال", O + ID_OPEN_CRASHES, false, true, L"\uE7BA");
            Tip(O + ID_OPEN_CRASHES, L"إذا توقّف البرنامج فجأة يُحفظ هنا تقرير يساعد في إصلاح الخطأ");
            y += 40;
            // التحديثات
            if (g_update.newer)
            {
                Hint(L"يتوفر إصدار جديد: " + LtrText(g_update.version), Theme::Green);
                DrawButton(Rect(r - 220, y, r, y + 32), L"تنزيل الإصدار " + LtrText(g_update.version), O + ID_GET_UPDATE, true, true);
                y += 40;
            }
            DrawButton(Rect(r - 200, y, r, y + 32), g_updateChecking ? L"جارٍ التحقق…" : L"التحقق من التحديثات",
                       O + ID_CHECK_UPDATE, false, !g_updateChecking);
            y += 40;
            Hint(L"اللغة: العربية (لغات أخرى لاحقًا)", Theme::Off);
        });
        break;
    }
    }
    return y - top;
}
