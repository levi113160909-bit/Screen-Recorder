// =============================================================
//  AppPanels.cpp - فتح النوافذ الداخلية: الإعدادات، المعرض، المشغّل، القص، فلاتر المايك
// =============================================================
#include "App.h"

// بتنستعمل قبل تعريفها بهالملف
void OpenFolder();

// نافذة فلاتر المايك (نافذة داخلية، شوف MicFiltersPanel.cpp)
void ShowMicFilters()
{
    g_panels.Open(std::make_unique<MicFiltersPanel>(
        g_micAudio,
        [] { return g_micOn && g_micAudio.IsRunning(); },
        [] { SaveSettings(); }));
}

// نافذة القص. back = وين نرجع بعدها (المعرض لحالو، أو صفحة المعرض بالإعدادات)
void OpenTrim(const std::wstring& path, std::function<void()> back)
{
    g_panels.Open(std::make_unique<TrimPanel>(path,
        [back](const std::wstring& out)
        {
            Notify(Glyph::Check, Theme::Green, L"حُفظ المقطع", FileNameOf(out));
            back();
        },
        [back] { back(); }));
}

std::unique_ptr<Panel> MakeGalleryPanel(std::function<void()> back)
{
    GalleryHooks h;
    h.folders = []
    {
        std::vector<std::pair<std::wstring, int>> f = { { g_folder, 0 }, { g_folder + L"\\Streams", 2 },
                                                         { g_folder + L"\\Screenshots", 3 } };
        const std::wstring replay = ReplayFolder();
        if (_wcsicmp(replay.c_str(), g_folder.c_str()) != 0)
            f.push_back({ replay, 1 });
        return f;
    };
    h.trim       = [back](const std::wstring& p) { OpenTrim(p, back); };
    h.play       = [back](const std::wstring& p, bool image)
    {
        PlayerHooks ph;
        ph.trim = [back](const std::wstring& path) { OpenTrim(path, back); };
        ph.back = back;
        g_panels.Open(std::make_unique<PlayerPanel>(p, image, std::move(ph)));
    };
    h.rename     = [](std::wstring& name) { return InputBox(L"الاسم الجديد", name, false, false, 120); };
    h.openFolder = [] { OpenFolder(); };
    return std::make_unique<GalleryPanel>(std::move(h));
}

void OpenGallery()
{
    g_panels.Open(MakeGalleryPanel([] { OpenGallery(); }));
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
    g_panels.Open(std::make_unique<SettingsPanel>(std::move(h), MakeReplayPanel(), MakeHotkeysPanel(),
                                                  MakeGalleryPanel([] { OpenSettings(SP_GALLERY); }), page));
}
