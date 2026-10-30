// =============================================================
//  VirtualCam.cpp - شوف VirtualCam.h
// =============================================================
#include "VirtualCam.h"
#include "Scene.h"
#include "../VirtualCamera/VirtualCameraShared.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    std::atomic<int>       g_state{ 0 };          // 0 مطفية ، 1 عم تشتغل ، 2 شغّالة ، 3 عم تتوقف
    std::atomic<bool>      g_stop{ false };
    std::atomic<ULONGLONG> g_lastWatched{ 0 };    // آخر مرة شفنا برنامج عم يعرضها

    using CreateVirtualCameraFn = HRESULT(WINAPI*)(MFVirtualCameraType, MFVirtualCameraLifetime, MFVirtualCameraAccess,
                                                   LPCWSTR, LPCWSTR, const GUID*, ULONG, IMFVirtualCamera**);

    CreateVirtualCameraFn CreateFn()
    {
        static CreateVirtualCameraFn fn = []
        {
            HMODULE m = LoadLibraryExW(L"mfsensorgroup.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            return m ? reinterpret_cast<CreateVirtualCameraFn>(GetProcAddress(m, "MFCreateVirtualCamera")) : nullptr;
        }();
        return fn;
    }

    void Post(HWND hwnd, WPARAM code, const std::wstring& error = L"")
    {
        std::wstring* e = error.empty() ? nullptr : new std::wstring(error);
        if (!PostMessageW(hwnd, WM_VCAM_RESULT, code, (LPARAM)e))
            delete e;
    }

    std::wstring ExeDir()
    {
        wchar_t buf[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
        std::wstring p(buf, n);
        return p.substr(0, p.find_last_of(L'\\'));
    }

    std::wstring InstallDir()
    {
        PWSTR pd = nullptr;
        std::wstring out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd)))
        {
            out = std::wstring(pd) + L"\\ScreenRecorder\\VirtualCamera";
            CoTaskMemFree(pd);
        }
        return out;
    }

    bool Exists(const std::wstring& p)
    {
        const DWORD a = GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    // المسار المسجّل حاليًا بويندوز (أو فاضي)
    std::wstring RegisteredPath()
    {
        const std::wstring key = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + VCAM_CLSID_STRING + L"\\InprocServer32";
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD size = sizeof(buf);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) != ERROR_SUCCESS)
            return L"";
        return buf;
    }

    // اسم النسخة: بيتغيّر مع كل نسخة جديدة من الملف (لأن القديمة ممكن تكون محمّلة بالخدمة وما بتنكتب فوقها)
    std::wstring VersionedName(const std::wstring& src)
    {
        WIN32_FILE_ATTRIBUTE_DATA d{};
        if (!GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &d))
            return L"";
        unsigned long long h = 1469598103934665603ull;
        const unsigned long long parts[] = { d.nFileSizeLow, d.nFileSizeHigh, d.ftLastWriteTime.dwLowDateTime, d.ftLastWriteTime.dwHighDateTime };
        for (unsigned long long v : parts) { h ^= v; h *= 1099511628211ull; }
        wchar_t name[64];
        swprintf_s(name, L"VirtualCamera_%08llx.dll", h & 0xFFFFFFFFull);
        return name;
    }

    // التثبيت (مرة وحدة لكل نسخة): نسخ لـ ProgramData + تسجيل بـ HKLM ، بصلاحية المسؤول
    // بيرجّع "" إذا تمام ، أو سبب الخطأ
    std::wstring EnsureInstalled(HWND owner)
    {
        const std::wstring src = ExeDir() + L"\\VirtualCamera.dll";
        if (!Exists(src))
            return L"الملف VirtualCamera.dll غير موجود بجانب البرنامج";
        const std::wstring dir = InstallDir(), name = VersionedName(src);
        if (dir.empty() || name.empty())
            return L"تعذّر تحديد مكان تثبيت الكاميرا الافتراضية";
        const std::wstring target = dir + L"\\" + name;
        if (Exists(target) && _wcsicmp(RegisteredPath().c_str(), target.c_str()) == 0)
            return L"";                                          // مثبّتة من قبل

        // أمر واحد بصلاحية المسؤول (طلب موافقة واحد):
        //  إنشاء المجلد ، النسخ ، السماح للكل بالقراءة (خدمة الكاميرا بتشتغل كـ Local Service) ، التسجيل
        const std::wstring cmd =
            L"/s /c \"(mkdir \"" + dir + L"\" 2>nul) & copy /y \"" + src + L"\" \"" + target + L"\" >nul && "
            L"icacls \"" + dir + L"\" /grant \"*S-1-1-0:(OI)(CI)RX\" /T >nul & "
            L"regsvr32 /s \"" + target + L"\"\"";

        HRESULT hrCo = E_FAIL;
        std::wstring error;
        // ShellExecuteEx بيحب STA: منعملها بخيط صغير لحالها
        std::thread([&]
        {
            hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            SHELLEXECUTEINFOW sei{ sizeof(sei) };
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            sei.hwnd = owner;
            sei.lpVerb = L"runas";
            sei.lpFile = L"cmd.exe";
            sei.lpParameters = cmd.c_str();
            sei.nShow = SW_HIDE;
            if (!ShellExecuteExW(&sei))
            {
                error = GetLastError() == ERROR_CANCELLED
                    ? L"يلزم السماح مرة واحدة (صلاحية المسؤول) لتثبيت الكاميرا الافتراضية"
                    : L"تعذّر تثبيت الكاميرا الافتراضية";
            }
            else if (sei.hProcess)
            {
                WaitForSingleObject(sei.hProcess, 60'000);
                CloseHandle(sei.hProcess);
            }
            if (SUCCEEDED(hrCo)) CoUninitialize();
        }).join();
        if (!error.empty())
            return error;
        if (!Exists(target) || _wcsicmp(RegisteredPath().c_str(), target.c_str()) != 0)
            return L"تعذّر تسجيل الكاميرا الافتراضية في ويندوز";
        return L"";
    }

    // ---------------------------------------------------------
    //  الخيط: تشغيل الكاميرا ، وبعدين رسم المشهد لها لحد ما تنطفي
    // ---------------------------------------------------------
    void Worker(HWND notify, Scene* scene)
    {
        const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto finish = [&](WPARAM code, const std::wstring& err)
        {
            if (SUCCEEDED(hrCo)) CoUninitialize();
            g_state = 0;
            Post(notify, code, err);
        };

        const std::wstring installError = EnsureInstalled(notify);
        if (!installError.empty())
            return finish(0, installError);
        if (g_stop)
            return finish(2, L"");

        ComPtr<IMFVirtualCamera> vcam;
        HRESULT hr = CreateFn()(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session,
                                MFVirtualCameraAccess_CurrentUser, VCAM_FRIENDLY_NAME, VCAM_CLSID_STRING, nullptr, 0, &vcam);
        if (SUCCEEDED(hr))
            hr = vcam->Start(nullptr);
        if (FAILED(hr))
        {
            if (vcam) vcam->Remove();
            wchar_t buf[160];
            swprintf_s(buf, L"تعذّر تشغيل الكاميرا الافتراضية (0x%08X)", (unsigned)hr);
            std::wstring msg = buf;
            if (hr == E_ACCESSDENIED)
                msg = L"رفض ويندوز تشغيل الكاميرا الافتراضية: تأكّد من السماح للتطبيقات باستخدام الكاميرا من إعدادات الخصوصية";
            return finish(0, msg);
        }

        g_state = 2;
        Post(notify, 1);

        // ---- الرسم ----
        {
            Compositor comp(false);                 // بلا مؤشر الماوس
            HANDLE mapping = nullptr;
            void* view = nullptr;
            ULONGLONG lastTry = 0;
            auto next = std::chrono::steady_clock::now();
            const auto frame = std::chrono::microseconds(33'333);
            while (!g_stop)
            {
                const ULONGLONG now = GetTickCount64();
                if (!view)
                {
                    // الذاكرة بتنعمل لما برنامج يفتح الكاميرا (من خدمة ويندوز)
                    if (now - lastTry >= 1000)
                    {
                        lastTry = now;
                        mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, VCAM_MAPPING_NAME);
                        if (mapping)
                        {
                            view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VCAM_TOTAL);
                            if (!view)
                            {
                                CloseHandle(mapping);
                                mapping = nullptr;
                            }
                        }
                    }
                    Sleep(100);
                    continue;
                }
                auto* h = static_cast<VCamHeader*>(view);
                const ULONGLONG hb = (ULONGLONG)h->heartbeat;
                if (h->magic != (LONG)VCAM_MAGIC || hb + 3000 < now)
                {
                    Sleep(100);                     // ما حدا عم يعرضها: ما منرسم شي
                    next = std::chrono::steady_clock::now();
                    continue;
                }
                g_lastWatched = now;

                int cw = 0, ch = 0;
                std::vector<SceneLayer> layers;
                FadeInfo fade;
                scene->Snapshot(cw, ch, layers, fade);
                if (cw >= 16 && ch >= 16)
                {
                    const double s = std::min({ 1.0, (double)VCAM_MAX_W / cw, (double)VCAM_MAX_H / ch });
                    const int fw = std::max(16, (int)(cw * s) & ~1), fh = std::max(16, (int)(ch * s) & ~1);
                    const LONG64 n = h->latest + 1;
                    const uint32_t slot = (uint32_t)(n % VCAM_SLOTS);
                    VCamSlot& sl = h->slots[slot];
                    InterlockedExchange64(&sl.seq, 2 * n + 1);           // فردي = عم تنكتب
                    ComposeParams p;
                    p.out = VCamSlotData(view, slot);
                    p.outW = fw;
                    p.outH = fh;
                    p.stride = fw * 4;
                    p.fade = fade;
                    comp.Compose(layers, cw, ch, p);
                    sl.width = fw;
                    sl.height = fh;
                    sl.tick = (LONG64)GetTickCount64();
                    InterlockedExchange64(&sl.seq, 2 * n + 2);           // جاهزة
                    InterlockedExchange64(&h->latest, n);
                }

                // 30 صورة بالثانية على شبكة وقت ثابتة
                next += frame;
                const auto t = std::chrono::steady_clock::now();
                if (next < t - frame * 3)
                    next = t;
                while (!g_stop && std::chrono::steady_clock::now() < next)
                    Sleep(1);
            }
            if (view) UnmapViewOfFile(view);
            if (mapping) CloseHandle(mapping);
        }

        // ملاحظة (من VCamSample): ما منستدعي Shutdown لأنها بتسكّر المصدر مرتين
        vcam->Remove();
        vcam.Reset();
        finish(2, L"");
    }
}

bool VirtualCamSupported()
{
    return CreateFn() != nullptr;
}

bool VirtualCamOn()
{
    return g_state == 2;
}

bool VirtualCamBusy()
{
    return g_state == 1 || g_state == 3;
}

int VirtualCamStateCode()
{
    return g_state;
}

bool VirtualCamInUse()
{
    return g_state == 2 && GetTickCount64() - g_lastWatched < 2000;
}

void VirtualCamStart(HWND notify, Scene& scene)
{
    int expected = 0;
    if (!g_state.compare_exchange_strong(expected, 1))
        return;
    if (!VirtualCamSupported())
    {
        g_state = 0;
        Post(notify, 0, L"الكاميرا الافتراضية تتطلّب ويندوز 11");
        return;
    }
    g_stop = false;
    std::thread(Worker, notify, &scene).detach();
}

void VirtualCamStop()
{
    if (g_state == 0)
        return;
    g_stop = true;
    if (g_state == 2)
        g_state = 3;
}

void VirtualCamShutdown()
{
    if (g_state == 0)
        return;
    VirtualCamStop();
    for (int i = 0; i < 40 && g_state != 0; ++i)       // حد أقصى ثانيتين
        Sleep(50);
}

std::wstring VirtualCamTakeError(LPARAM lParam)
{
    std::unique_ptr<std::wstring> e(reinterpret_cast<std::wstring*>(lParam));
    return e ? *e : std::wstring();
}
