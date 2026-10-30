// =============================================================
//  Recording.cpp - التسجيل: البدء والإيقاف والإيقاف المؤقت، العلامات (فصول يوتيوب)، ومراقبة مساحة القرص
// =============================================================
#include "App.h"

int  g_diskWarnLevel = 0;               // 0 تمام ، 1 نبّهنا (أقل من 10 دقايق) ، 2 وقّفنا

std::vector<LONGLONG> g_markers;        // العلامات: بالملي ثانية من بداية التسجيل

int  g_pttReleaseTick = 0;      // اضغط لتتكلم: إمتى نسكّر المايك بعد ما تترك الزر

bool g_pttWasOn = false;       // 0 تمام ، 1 نبّهنا (أقل من 10 دقايق) ، 2 وقّفنا

// بتنستعمل قبل تعريفها بهالملف
void StopRecording();

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
    opt.variableFps    = (g_fpsChoice == FPS_VARIABLE);
    opt.codec          = (VideoCodec)g_codec;
    if (g_systemOn && g_systemAudio.IsRunning()) opt.audio.push_back(&g_systemAudio);
    if (g_micOn && g_micAudio.IsRunning())       opt.audio.push_back(&g_micAudio);
    if (AnySceneHasApps())                       opt.audio.push_back(&g_appMixer.Output());

    // القرص شبه ممتلئ؟ منبّه قبل ما نبلّش
    {
        const ULONGLONG freeB = FreeBytes(g_folder);
        if (freeB > 0 && freeB < 1ull * 1024 * 1024 * 1024)
            Notify(Glyph::Warning, Theme::Yellow, L"مساحة القرص قليلة", L"أقل من 1 غيغابايت متاح للتسجيل");
    }
    g_diskWarnLevel = 0;
    g_markers.clear();
    const HRESULT hr = g_recorder.Start(opt);
    if (SUCCEEDED(hr))
    {
        g_lastFile = opt.path;
        // إذا اخترت بطاقة رسومات معيّنة وويندوز استعمل غيرها منخبّرك
        static const wchar_t* codecNames[] = { L"H.264", L"HEVC", L"AV1" };
        // منقارن الشركة بس (NVIDIA / AMD / Intel): نفس الكرت بيعطي اسم مختلف لكل صيغة (H.264 / HEVC)
        auto vendor = [](const std::wstring& n) { return n.substr(0, n.find(L' ')); };
        if (g_encoderMode == 1 && !g_encoderName.empty() && vendor(g_recorder.EncoderName()) != vendor(g_encoderName))
            ShowMessage(L"تعذّر استخدام " + g_encoderName + L" فاستُخدم " + g_recorder.EncoderName(), true);
        else if ((int)g_recorder.CodecUsed() != g_codec)
            ShowMessage(std::wstring(codecNames[g_codec]) + L" غير مدعوم على جهازك، فاستُخدم " + codecNames[(int)g_recorder.CodecUsed()], false);
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

// ---------- العلامات (زر بتكبسو وقت التسجيل عند اللحظات الحلوة) ----------
std::wstring ChapterTime(LONGLONG ms)
{
    const LONGLONG t = ms / 1000;
    wchar_t buf[32];
    if (t >= 3600) swprintf_s(buf, L"%lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60);
    else           swprintf_s(buf, L"%02lld:%02lld", t / 60, t % 60);
    return buf;
}

void AddMarker()
{
    if (!g_recorder.IsRecording())
    {
        Notify(Glyph::Flag, Theme::Yellow, L"العلامات تعمل أثناء التسجيل", L"ابدأ التسجيل ثم اضغط الزر عند اللحظة المهمة");
        return;
    }
    const LONGLONG ms = g_recorder.ElapsedMs();
    g_markers.push_back(ms);
    Notify(Glyph::Flag, Theme::Accent, L"علامة " + std::to_wstring(g_markers.size()), L"عند " + ChapterTime(ms));
}

// ملف نصي جنب الفيديو: أوقات العلامات (وبصيغة فصول يوتيوب: تنلصق بالوصف مباشرة)
void WriteMarkers(const std::wstring& videoPath)
{
    if (g_markers.empty() || videoPath.empty())
        return;
    const size_t dot = videoPath.find_last_of(L'.');
    const std::wstring path = (dot == std::wstring::npos ? videoPath : videoPath.substr(0, dot)) + L"_علامات.txt";
    std::wstring text = L"علامات التسجيل: " + FileNameOf(videoPath) + L"\r\n";
    text += L"(انسخ الأسطر التالية إلى وصف فيديو يوتيوب لتصبح فصولًا)\r\n\r\n";
    text += L"00:00 البداية\r\n";
    for (size_t i = 0; i < g_markers.size(); ++i)
        text += ChapterTime(g_markers[i]) + L" علامة " + std::to_wstring(i + 1) + L"\r\n";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), utf8.data(), n, nullptr, nullptr);
    DWORD w = 0;
    const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
    WriteFile(h, bom, 3, &w, nullptr);
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &w, nullptr);
    CloseHandle(h);
}

void FinishRecording(HRESULT hr)
{
    WriteMarkers(g_lastFile);
    const size_t markers = g_markers.size();
    g_markers.clear();
    if (SUCCEEDED(hr) && markers > 0)
    {
        ShowMessage(L"✓ تم الحفظ: " + FileNameOf(g_lastFile) + L" • مع " + std::to_wstring(markers) + L" علامات", false);
        Notify(Glyph::Check, Theme::Green, L"تم حفظ التسجيل", std::to_wstring(markers) + L" علامات محفوظة بجانب الفيديو");
    }
    else if (SUCCEEDED(hr))
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

// ---------- مساحة القرص ----------
// المساحة الفاضية بالبايت على قرص المجلد (0 = ما عرفنا)
ULONGLONG FreeBytes(const std::wstring& folder)
{
    ULARGE_INTEGER freeToCaller{};
    if (!GetDiskFreeSpaceExW(folder.c_str(), &freeToCaller, nullptr, nullptr))
        return 0;
    return freeToCaller.QuadPart;
}

// "120 غيغابايت • تكفي نحو 9 ساعات" بالإعدادات الحالية
std::wstring DiskSummary(ULONGLONG freeB, UINT32 bitrate, bool& low)
{
    const double gb = freeB / 1e9;
    const double bytesPerSec = (bitrate + 192'000.0 * 2) / 8.0;     // فيديو + صوت تقريباً
    const double hours = freeB / bytesPerSec / 3600.0;
    low = hours < 1.0;
    wchar_t buf[160];
    if (hours >= 1.0)
        swprintf_s(buf, L"المساحة المتاحة: %.0f غيغابايت • تكفي نحو %.0f ساعة تسجيل بالإعدادات الحالية", gb, std::floor(hours));
    else
        swprintf_s(buf, L"المساحة المتاحة: %.1f غيغابايت • تكفي نحو %.0f دقيقة فقط", gb, hours * 60);
    return buf;
}

void CheckDiskSpace()
{
    if (!g_recorder.IsRecording())
    {
        g_diskWarnLevel = 0;
        return;
    }
    const ULONGLONG freeB = FreeBytes(g_folder);
    if (freeB == 0)
        return;
    const double minutes = freeB / ((CurrentBitrate() + 384'000.0) / 8.0) / 60.0;
    if (freeB < 300ull * 1024 * 1024 && g_diskWarnLevel < 2)
    {
        g_diskWarnLevel = 2;
        StopRecording();                          // الملف بينحفظ سليم
        Notify(Glyph::Warning, Theme::Red, L"توقف التسجيل: القرص ممتلئ تقريبًا", L"حُفظ التسجيل • أفرغ بعض المساحة");
        ShowMessage(L"توقف التسجيل تلقائيًا لأن القرص ممتلئ تقريبًا (حُفظ الملف)", true);
    }
    else if (minutes < 10 && g_diskWarnLevel < 1)
    {
        g_diskWarnLevel = 1;
        wchar_t sub[96];
        swprintf_s(sub, L"تكفي المساحة لنحو %.0f دقيقة فقط", std::max(1.0, minutes));
        Notify(Glyph::Warning, Theme::Yellow, L"القرص يقترب من الامتلاء", sub);
    }
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
