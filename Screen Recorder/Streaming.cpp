// =============================================================
//  Streaming.cpp - البث المباشر: الوجهات (يوتيوب، تويتش...)، شعاراتها، مفاتيحها المشفّرة، والبدء والإيقاف
// =============================================================
#include "App.h"

// أيقونات المنصات:
//  - إذا حطّيت صورة الشعار الرسمي بمجلد الأيقونات (youtube.png ...) منستعملها
//  - غير هيك منرسم أيقونة بسيطة بلون المنصة
const wchar_t* PLATFORM_FILES[] = { L"youtube", L"twitch", L"facebook", L"kick", L"tiktok", L"custom" };

ComPtr<IWICImagingFactory>    g_wic;

ComPtr<IWICFormatConverter>   g_logoSrc[PLATFORM_COUNT];

bool                          g_logoTried[PLATFORM_COUNT] = {};

ComPtr<ID2D1RenderTarget>     g_logoRt;                  // الرسّام اللي انعملت عليه الصور

ComPtr<ID2D1Bitmap>           g_logoBmp[PLATFORM_COUNT];

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
        fwprintf(f, L"T\t%d\t%d\t%ls\t%ls\t%ls\t%d\t%d\t%u\n", (int)t.platform, t.enabled ? 1 : 0,
                 Clean(t.name).c_str(), Clean(t.url).c_str(), ProtectText(t.key).c_str(), t.vertical ? 1 : 0,
                 t.codec, t.kbps);
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
        t.codec    = p.size() >= 8 ? std::clamp(_wtoi(p[7].c_str()), 0, 1) : 0;
        t.kbps     = p.size() >= 9 ? (UINT32)std::clamp(_wtoi(p[8].c_str()), 0, 50000) : 0;
        if (t.kbps && t.kbps < 1000) t.kbps = 1000;
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

// معدّل البت الفعلي لمنصة ، والترميز (HEVC بس للمنصات اللي بتدعمه)
UINT32 TargetKbps(const StreamTarget& t) { return t.kbps ? t.kbps : g_streamKbps; }

bool   TargetHevc(const StreamTarget& t) { return t.codec == 1 && PlatformSupportsHevc(t.platform); }

// المنصات المفعّلة اللي بتتشارك نفس النسخة المضغوطة مع المنصة رقم index
std::wstring SharedWith(int index)
{
    std::wstring out;
    if (index < 0 || index >= (int)g_targets.size())
        return out;
    const StreamTarget& a = g_targets[index];
    for (int i = 0; i < (int)g_targets.size(); ++i)
    {
        const StreamTarget& b = g_targets[i];
        if (i == index || !b.enabled || b.vertical != a.vertical || TargetHevc(b) != TargetHevc(a) || TargetKbps(b) != TargetKbps(a))
            continue;
        if (!out.empty()) out += L"، ";
        out += b.name;
    }
    return out;
}

// مجموع الرفع (كيلوبت/ث) وكم نسخة منضغطة ، للمنصات المفعّلة
void UploadEstimate(UINT32& totalKbps, int& encodes)
{
    totalKbps = 0;
    std::vector<std::tuple<bool, bool, UINT32>> groups;
    for (const StreamTarget& t : g_targets)
    {
        if (!t.enabled)
            continue;
        totalKbps += TargetKbps(t) + 160;            // فيديو + صوت
        const auto key = std::make_tuple(t.vertical, TargetHevc(t), TargetKbps(t));
        if (std::find(groups.begin(), groups.end(), key) == groups.end())
            groups.push_back(key);
    }
    encodes = (int)groups.size();
}

void OpenTargetPanel(int index)
{
    TargetPanelHooks h;
    h.target = [index]() -> StreamTarget* { return index < (int)g_targets.size() ? &g_targets[index] : nullptr; };
    h.defaultKbps = [] { return g_streamKbps; };
    h.format = [index](UINT32& w, UINT32& hh, UINT32& fps)
    {
        fps = StreamFps();
        if (index < (int)g_targets.size() && g_targets[index].vertical)
        {
            w  = g_streamRes == 2 ? 720 : 1080;
            hh = g_streamRes == 2 ? 1280 : 1920;
        }
        else
            StreamSize(w, hh);
    };
    h.sharedWith = [index] { return SharedWith(index); };
    h.changed = [] { SaveTargets(); };
    g_panels.Open(std::make_unique<TargetPanel>(std::move(h)));
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
                                t.vertical ? L"✓   بث عمودي (9:16)" : L"      بث عمودي (9:16)", L"", L"حذف الوجهة",
                                L"", L"الترميز ومعدّل البت…" }, -1, pt);
    switch (pick)
    {
    case 7: OpenTargetPanel(index); break;
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
    opt.fps       = StreamFps();
    opt.videoKbps = g_streamKbps;
    opt.encoder   = (EncoderMode)g_encoderMode;
    opt.adaptive  = g_streamAdaptive;
    if (g_streamSaveCopy)
    {
        const std::wstring dir = g_folder + L"\\Streams";
        CreateDirectoryW(g_folder.c_str(), nullptr);
        CreateDirectoryW(dir.c_str(), nullptr);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t name[80];
        swprintf_s(name, L"\\Stream_%04d-%02d-%02d_%02d-%02d-%02d.mp4", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        opt.saveCopyPath = dir + name;
    }
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
        if (!g_streamer.Warning().empty())
            ShowMessage(g_streamer.Warning(), true);
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

void StopStreaming(const std::wstring& message)
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
