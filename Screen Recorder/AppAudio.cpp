// =============================================================
//  AppAudio.cpp - أصوات البرامج (شوف AppAudio.h)
// =============================================================
#include "AppAudio.h"
#include <tlhelp32.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <iterator>

#pragma comment(lib, "version.lib")

using Microsoft::WRL::ComPtr;

std::wstring LowerPath(std::wstring s)
{
    for (wchar_t& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

namespace
{
    std::wstring Lower(const std::wstring& s) { return LowerPath(s); }

    std::wstring FileNameOnly(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? path : path.substr(slash + 1);
    }

    std::wstring ProcessPath(DWORD pid)
    {
        std::wstring path;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h)
            return path;
        wchar_t buf[MAX_PATH * 2];
        DWORD size = (DWORD)std::size(buf);
        if (QueryFullProcessImageNameW(h, 0, buf, &size))
            path.assign(buf, size);
        CloseHandle(h);
        return path;
    }

    // اسم البرنامج الحلو من معلومات الملف (مثلاً "Google Chrome")، وإلا اسم الملف
    std::wstring FriendlyName(const std::wstring& exePath)
    {
        std::wstring name = FileNameOnly(exePath);
        if (name.size() > 4 && Lower(name.substr(name.size() - 4)) == L".exe")
            name.resize(name.size() - 4);

        DWORD dummy = 0;
        const DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &dummy);
        if (size == 0)
            return name;
        std::vector<BYTE> data(size);
        if (!GetFileVersionInfoW(exePath.c_str(), 0, size, data.data()))
            return name;

        struct Lang { WORD lang, codepage; }* langs = nullptr;
        UINT len = 0;
        if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void**)&langs, &len) || len < sizeof(Lang))
            return name;
        wchar_t key[64];
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", langs[0].lang, langs[0].codepage);
        wchar_t* desc = nullptr;
        if (VerQueryValueW(data.data(), key, (void**)&desc, &len) && desc && len > 1)
        {
            std::wstring d(desc);
            while (!d.empty() && d.back() == L' ') d.pop_back();
            if (!d.empty())
                return d;
        }
        return name;
    }

    struct EnumState
    {
        HWND                  exclude;
        DWORD                 selfPid;
        std::vector<AudioApp> apps;
    };

    BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp)
    {
        EnumState& st = *(EnumState*)lp;
        if (hwnd == st.exclude || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
            return TRUE;
        if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
            return TRUE;
        if (GetWindowTextLengthW(hwnd) == 0)
            return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0 || pid == st.selfPid)
            return TRUE;
        const std::wstring path = ProcessPath(pid);
        if (path.empty())
            return TRUE;
        const std::wstring file = Lower(FileNameOnly(path));
        // برامج ويندوز نفسها (سطح المكتب، الإعدادات...) ما إلها صوت مفيد
        if (file == L"explorer.exe" || file == L"applicationframehost.exe" || file == L"textinputhost.exe" ||
            file == L"systemsettings.exe" || file == L"shellexperiencehost.exe" || file == L"searchhost.exe")
            return TRUE;
        for (const AudioApp& a : st.apps)
            if (Lower(a.exePath) == Lower(path))
                return TRUE;            // نفس البرنامج بأكتر من نافذة: مرة وحدة
        wchar_t title[256] = {};
        GetWindowTextW(hwnd, title, 256);
        AudioApp app;
        app.pid     = pid;
        app.exePath = path;
        app.name    = FriendlyName(path);
        app.title   = title;
        st.apps.push_back(app);
        return TRUE;
    }

    // "جلسات الصوت" بويندوز: كل برنامج عم يطلّع (أو طلّع) صوت إله جلسة
    struct SessionInfo { DWORD pid; bool active; };
    std::vector<SessionInfo> ListSessions()
    {
        std::vector<SessionInfo> list;
        ComPtr<IMMDeviceEnumerator> en;
        ComPtr<IMMDeviceCollection> devices;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))) ||
            FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)))
            return list;
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT d = 0; d < count; ++d)
        {
            ComPtr<IMMDevice> device;
            ComPtr<IAudioSessionManager2> manager;
            ComPtr<IAudioSessionEnumerator> sessions;
            if (FAILED(devices->Item(d, &device)) ||
                FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)manager.GetAddressOf())) ||
                FAILED(manager->GetSessionEnumerator(&sessions)))
                continue;
            int n = 0;
            sessions->GetCount(&n);
            for (int i = 0; i < n; ++i)
            {
                ComPtr<IAudioSessionControl>  ctl;
                ComPtr<IAudioSessionControl2> ctl2;
                if (FAILED(sessions->GetSession(i, &ctl)) || FAILED(ctl.As(&ctl2)))
                    continue;
                if (ctl2->IsSystemSoundsSession() == S_OK)   // أصوات ويندوز نفسها
                    continue;
                DWORD pid = 0;
                AudioSessionState state = AudioSessionStateInactive;
                if (FAILED(ctl2->GetProcessId(&pid)) || pid == 0 || FAILED(ctl->GetState(&state)))
                    continue;
                list.push_back({ pid, state == AudioSessionStateActive });
            }
        }
        return list;
    }

    // منلاقي البرنامج من جديد (بعد ما ينسكّر وينفتح): أول شي من النوافذ،
    // وإذا ما إله نافذة (مثلاً مصغّر جنب الساعة) من لائحة العمليات
    bool FindRunning(const std::wstring& exePath, AudioApp& out)
    {
        EnumState st{ nullptr, GetCurrentProcessId(), {} };     // النوافذ بس (بدون جلسات الصوت)
        EnumWindows(EnumProc, (LPARAM)&st);
        for (const AudioApp& a : st.apps)
            if (Lower(a.exePath) == Lower(exePath)) { out = a; return true; }

        const std::wstring file = Lower(FileNameOnly(exePath));
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return false;
        std::vector<PROCESSENTRY32W> matches;
        PROCESSENTRY32W pe{ sizeof(pe) };
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
            if (Lower(pe.szExeFile) == file)
                matches.push_back(pe);
        CloseHandle(snap);
        // منختار "الأب": العملية اللي أبوها مش نفس البرنامج (كروم مثلاً بيفتح عشرات العمليات)
        for (const PROCESSENTRY32W& m : matches)
        {
            bool parentSame = false;
            for (const PROCESSENTRY32W& o : matches)
                if (o.th32ProcessID == m.th32ParentProcessID) parentSame = true;
            if (!parentSame && Lower(ProcessPath(m.th32ProcessID)) == Lower(exePath))
            {
                out = { m.th32ProcessID, exePath, FriendlyName(exePath) };
                return true;
            }
        }
        return false;
    }
}

std::wstring AppFriendlyName(const std::wstring& exePath) { return FriendlyName(exePath); }

std::vector<std::wstring> ExePathsPlayingAudio()
{
    std::vector<std::wstring> paths;
    for (const SessionInfo& s : ListSessions())
    {
        if (!s.active)
            continue;
        const std::wstring p = Lower(ProcessPath(s.pid));
        if (!p.empty() && std::find(paths.begin(), paths.end(), p) == paths.end())
            paths.push_back(p);
    }
    return paths;
}

std::vector<AudioApp> ListAudioApps(HWND exclude)
{
    EnumState st{ exclude, GetCurrentProcessId(), {} };
    EnumWindows(EnumProc, (LPARAM)&st);

    // البرامج اللي إلها صوت بس ما إلها نافذة (مثلاً مصغّرة جنب الساعة)
    const std::vector<SessionInfo> sessions = ListSessions();
    for (const SessionInfo& s : sessions)
    {
        if (s.pid == st.selfPid)
            continue;
        const std::wstring path = ProcessPath(s.pid);
        if (path.empty())
            continue;
        bool known = false;
        for (AudioApp& a : st.apps)
            if (Lower(a.exePath) == Lower(path)) { known = true; break; }
        if (known)
            continue;
        // منوصل للعملية "الأب" تبع هالبرنامج (كروم مثلاً: الصوت من عملية فرعية)
        AudioApp app;
        if (!FindRunning(path, app))
            app = { s.pid, path, FriendlyName(path), L"", false };
        st.apps.push_back(app);
    }

    // مين عم يطلّع صوت هلق؟ (منقارن بالمسار لأن الصوت ممكن يطلع من عملية فرعية)
    for (const SessionInfo& s : sessions)
    {
        if (!s.active)
            continue;
        const std::wstring path = Lower(ProcessPath(s.pid));
        for (AudioApp& a : st.apps)
            if (Lower(a.exePath) == path)
                a.playing = true;
    }

    std::sort(st.apps.begin(), st.apps.end(), [](const AudioApp& a, const AudioApp& b)
    {
        if (a.playing != b.playing)
            return a.playing;
        return Lower(a.name) < Lower(b.name);
    });
    return st.apps;
}

// =============================================================
//  الخلّاط
// =============================================================
AppAudioMixer::AppAudioMixer()
{
    m_output.SetGenerator([this](float* out, size_t frames) { Generate(out, frames); });
}

void AppAudioMixer::Start()
{
    m_output.Start();
}

void AppAudioMixer::Stop()
{
    m_output.Stop();
    std::vector<Child> old;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        old.swap(m_children);
    }
    for (Child& c : old)
        Disconnect(c);
}

void AppAudioMixer::Connect(Child& c, const AudioApp& app)
{
    auto src = std::make_unique<AudioSource>(AudioSource::Kind::Application);
    src->SetProcess(app.pid, c.e.name);
    src->SetVolume(c.e.volume);
    src->SetMuted(c.e.muted);
    c.lastTry = GetTickCount64();
    if (!src->Start())
        return;
    c.tap = src->AddTap();
    src->SetArmed(c.tap, true, true);
    c.process = OpenProcess(SYNCHRONIZE, FALSE, app.pid);
    c.primed  = false;
    c.src     = std::move(src);
}

void AppAudioMixer::Disconnect(Child& c)
{
    if (c.src)
    {
        c.src->Stop();
        c.src->RemoveTap(c.tap);
        c.src.reset();
    }
    if (c.process)
    {
        CloseHandle(c.process);
        c.process = nullptr;
    }
}

void AppAudioMixer::SetEntries(const std::vector<AppAudioEntry>& entries)
{
    std::vector<Child> fresh(entries.size()), old;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        fresh[i].e = entries[i];
        AudioApp app;
        if (FindRunning(entries[i].exePath, app))
            Connect(fresh[i], app);
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        old.swap(m_children);
        m_children = std::move(fresh);
    }
    for (Child& c : old)       // منسكّر القدام برّا القفل (التوقيف بياخد شوي وقت)
        Disconnect(c);
}

std::vector<AppAudioEntry> AppAudioMixer::Entries()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AppAudioEntry> list;
    for (const Child& c : m_children)
        list.push_back(c.e);
    return list;
}

void AppAudioMixer::Add(const AudioApp& app)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const Child& c : m_children)
            if (Lower(c.e.exePath) == Lower(app.exePath))
                return;                      // موجود من قبل
    }
    Child c;
    c.e.exePath = app.exePath;
    c.e.name    = app.name;
    Connect(c, app);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_children.push_back(std::move(c));
}

void AppAudioMixer::Remove(int index)
{
    Child c;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index < 0 || index >= (int)m_children.size())
            return;
        c = std::move(m_children[index]);
        m_children.erase(m_children.begin() + index);
    }
    Disconnect(c);
}

size_t AppAudioMixer::Count()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_children.size();
}

AppAudioEntry AppAudioMixer::Entry(int index)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (index >= 0 && index < (int)m_children.size()) ? m_children[index].e : AppAudioEntry{};
}

bool AppAudioMixer::IsConnected(int index)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return index >= 0 && index < (int)m_children.size() && m_children[index].src && m_children[index].src->IsRunning();
}

float AppAudioMixer::TakePeak(int index)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= (int)m_children.size() || !m_children[index].src)
        return 0.0f;
    return m_children[index].src->TakePeak();
}

void AppAudioMixer::SetVolume(int index, float v)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= (int)m_children.size())
        return;
    Child& c = m_children[index];
    c.e.volume = std::clamp(v, 0.0f, 2.0f);
    if (c.src) c.src->SetVolume(c.e.volume);
}

void AppAudioMixer::SetMuted(int index, bool m)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= (int)m_children.size())
        return;
    Child& c = m_children[index];
    c.e.muted = m;
    if (c.src) c.src->SetMuted(m);
}

void AppAudioMixer::Reconnect()
{
    // منشتغل على نسخة برّا القفل لأن الوصل والفصل بياخدوا وقت
    std::vector<int> toConnect;
    std::vector<Child> dead;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const ULONGLONG now = GetTickCount64();
        for (int i = 0; i < (int)m_children.size(); ++i)
        {
            Child& c = m_children[i];
            const bool exited = c.process && WaitForSingleObject(c.process, 0) == WAIT_OBJECT_0;
            const bool broken = c.src && !c.src->IsRunning();
            if (exited || broken)             // البرنامج انسكّر: منفصله
            {
                Child d;
                d.src = std::move(c.src);
                d.tap = c.tap;
                d.process = c.process;
                c.process = nullptr;
                c.primed  = false;
                dead.push_back(std::move(d));
            }
            // البرامج المش موصولة: منجرّب كل 3 ثواني (إذا فشل الوصل منستنى أكتر)
            if (!c.src && now - c.lastTry > 3000)
                toConnect.push_back(i);
        }
    }
    for (Child& d : dead)
        Disconnect(d);

    for (int i : toConnect)
    {
        Child probe;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (i >= (int)m_children.size() || m_children[i].src)
                continue;
            probe.e = m_children[i].e;
            m_children[i].lastTry = GetTickCount64();
        }
        AudioApp app;
        if (!FindRunning(probe.e.exePath, app))
            continue;
        Connect(probe, app);
        if (!probe.src)
            continue;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // ممكن اللائحة تغيّرت بالنص (حذف أو تبديل مشهد): منتأكد إنه لسا نفس البرنامج
            if (i < (int)m_children.size() && !m_children[i].src && m_children[i].e.exePath == probe.e.exePath)
            {
                Child& c = m_children[i];
                probe.src->SetVolume(c.e.volume);
                probe.src->SetMuted(c.e.muted);
                c.src     = std::move(probe.src);
                c.tap     = probe.tap;
                c.process = probe.process;
                c.primed  = false;
                probe.process = nullptr;
            }
        }
        Disconnect(probe);     // إذا ما انضاف (اللائحة تغيّرت) منسكّره
    }
}

// -------------------------------------------------------------
// الخلط: كل برنامج منجمع صوته فوق التاني
// -------------------------------------------------------------
void AppAudioMixer::Generate(float* out, size_t frames)
{
    // قبل ما ناخد من برنامج، منستنى يتجمّع عنده 30 ملي ثانية زيادة
    // (الصوت بيوصل دفعات، فهيك ما بيصير تقطيع)
    constexpr size_t PRIME = AudioSource::SAMPLE_RATE * 30 / 1000;
    constexpr size_t KEEP  = AudioSource::SAMPLE_RATE / 10;      // أقصى تأخير 100 ملي ثانية

    std::lock_guard<std::mutex> lock(m_mutex);
    m_tmp.resize(frames * AudioSource::CHANNELS);
    for (Child& c : m_children)
    {
        if (!c.src)
            continue;
        const size_t avail = c.src->Available(c.tap);
        if (!c.primed)
        {
            if (avail < frames + PRIME)
                continue;
            c.primed = true;
        }
        const size_t got = c.src->Read(c.tap, m_tmp.data(), frames);
        for (size_t i = 0; i < got * AudioSource::CHANNELS; ++i)
            out[i] += m_tmp[i];
        if (got < frames)
            c.primed = false;            // خلص الصوت (البرنامج سكت): منرجع نستنى
        c.src->KeepNewest(c.tap, frames + KEEP);
    }
}
