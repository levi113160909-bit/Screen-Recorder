// =============================================================
//  Elevation.cpp - "التشغيل كمسؤول" (شوف Elevation.h)
// =============================================================
#include "Elevation.h"
#include <shellapi.h>
#include <taskschd.h>
#include <wrl/client.h>
#define SECURITY_WIN32
#include <security.h>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    const wchar_t TASK_NAME[] = L"ScreenRecorder";     // نفس الاسم بـ installer\ScreenRecorder.iss

    struct Bstr
    {
        BSTR b;
        explicit Bstr(const wchar_t* s) : b(SysAllocString(s)) {}
        explicit Bstr(const std::wstring& s) : b(SysAllocString(s.c_str())) {}
        ~Bstr() { SysFreeString(b); }
        Bstr(const Bstr&) = delete;
        Bstr& operator=(const Bstr&) = delete;
        operator BSTR() const { return b; }
    };

    // الاتصال بجدولة المهام + المجلد الرئيسي "\"
    bool Connect(ComPtr<ITaskService>& svc, ComPtr<ITaskFolder>& root)
    {
        if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&svc))))
            return false;
        VARIANT empty;
        VariantInit(&empty);
        if (FAILED(svc->Connect(empty, empty, empty, empty)))
            return false;
        return SUCCEEDED(svc->GetFolder(Bstr(L"\\"), &root)) && root;
    }

    // "الجهاز\المستخدم" (المهمة بتشتغل لما هالمستخدم يسجّل دخول بس)
    std::wstring CurrentUser()
    {
        wchar_t buf[512] = {};
        ULONG n = 512;
        if (GetUserNameExW(NameSamCompatible, buf, &n))
            return buf;
        DWORD m = 512;
        GetUserNameW(buf, &m);
        return buf;
    }
}

bool IsElevated()
{
    static const bool elevated = []
    {
        HANDLE t = nullptr;
        TOKEN_ELEVATION e{};
        DWORD len = 0;
        bool r = false;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t))
        {
            r = GetTokenInformation(t, TokenElevation, &e, sizeof(e), &len) && e.TokenIsElevated;
            CloseHandle(t);
        }
        return r;
    }();
    return elevated;
}

bool RelaunchElevated(const std::wstring& args)
{
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask        = SEE_MASK_NOASYNC;
    sei.lpVerb       = L"runas";
    sei.lpFile       = exe;
    sei.lpParameters = args.c_str();
    sei.nShow        = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;           // رفض الموافقة = ERROR_CANCELLED
}

bool StartupTaskExists()
{
    ComPtr<ITaskService> svc;
    ComPtr<ITaskFolder> root;
    if (!Connect(svc, root))
        return false;
    ComPtr<IRegisteredTask> task;
    return SUCCEEDED(root->GetTask(Bstr(TASK_NAME), &task)) && task;
}

bool CreateStartupTask(const std::wstring& exe, const std::wstring& args)
{
    ComPtr<ITaskService> svc;
    ComPtr<ITaskFolder> root;
    if (!Connect(svc, root))
        return false;
    ComPtr<ITaskDefinition> def;
    if (FAILED(svc->NewTask(0, &def)))
        return false;
    const std::wstring user = CurrentUser();

    ComPtr<IRegistrationInfo> info;
    if (SUCCEEDED(def->get_RegistrationInfo(&info)))
    {
        info->put_Author(Bstr(L"Screen Recorder"));
        info->put_Description(Bstr(L"Starts Screen Recorder (as administrator) when you sign in. Created from its settings."));
    }

    ComPtr<IPrincipal> principal;
    if (FAILED(def->get_Principal(&principal)))
        return false;
    principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);              // كمسؤول ، بلا طلب موافقة
    principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);      // جوّا جلستك (بيبيّن على الشاشة)
    principal->put_UserId(Bstr(user));

    ComPtr<ITaskSettings> settings;
    if (SUCCEEDED(def->get_Settings(&settings)))
    {
        settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);  // اللابتوب على البطارية كمان
        settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        settings->put_ExecutionTimeLimit(Bstr(L"PT0S"));          // بلا حد زمني (الافتراضي 3 أيام!)
        settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
        settings->put_Priority(5);                                 // متل البرامج العادية (الافتراضي 7 = أبطأ)
    }

    ComPtr<ITriggerCollection> triggers;
    ComPtr<ITrigger> trigger;
    ComPtr<ILogonTrigger> logon;
    if (FAILED(def->get_Triggers(&triggers)) || FAILED(triggers->Create(TASK_TRIGGER_LOGON, &trigger)) ||
        FAILED(trigger.As(&logon)))
        return false;
    logon->put_UserId(Bstr(user));
    logon->put_Delay(Bstr(L"PT5S"));                              // ثواني لحتى يجهز شريط المهام (أيقونة الساعة)

    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    if (FAILED(def->get_Actions(&actions)) || FAILED(actions->Create(TASK_ACTION_EXEC, &action)) ||
        FAILED(action.As(&exec)))
        return false;
    exec->put_Path(Bstr(exe));
    exec->put_Arguments(Bstr(args));

    VARIANT vUser;
    VariantInit(&vUser);
    vUser.vt = VT_BSTR;
    vUser.bstrVal = SysAllocString(user.c_str());
    VARIANT empty;
    VariantInit(&empty);
    VARIANT sddl;
    VariantInit(&sddl);
    sddl.vt = VT_BSTR;
    sddl.bstrVal = SysAllocString(L"");
    ComPtr<IRegisteredTask> registered;
    const HRESULT hr = root->RegisterTaskDefinition(Bstr(TASK_NAME), def.Get(), TASK_CREATE_OR_UPDATE, vUser, empty,
                                                    TASK_LOGON_INTERACTIVE_TOKEN, sddl, &registered);
    VariantClear(&vUser);
    VariantClear(&sddl);
    return SUCCEEDED(hr);
}

bool DeleteStartupTask()
{
    ComPtr<ITaskService> svc;
    ComPtr<ITaskFolder> root;
    if (!Connect(svc, root))
        return false;
    const HRESULT hr = root->DeleteTask(Bstr(TASK_NAME), 0);
    return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

void AllowMessageFromLowerPrivilege(HWND hwnd, UINT msg)
{
    if (msg)
        ChangeWindowMessageFilterEx(hwnd, msg, MSGFLT_ALLOW, nullptr);
}
