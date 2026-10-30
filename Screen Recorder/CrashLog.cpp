// =============================================================
//  CrashLog.cpp - سجل الأعطال (شوف CrashLog.h)
//
//  الطريقة: SetUnhandledExceptionFilter بيمسك أي خطأ ما انمسك.
//  منكتب التقرير من خيط جديد (لأن الخيط اللي وقع ممكن تكون ذاكرته
//  خلصت، متل Stack Overflow). وأخطاء C++ التانية (terminate، abort،
//  pure virtual، باراميتر غلط) منحوّلها لنفس الطريق.
// =============================================================
#include "CrashLog.h"
#include <dbghelp.h>
#include <shlobj.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <algorithm>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    // أكواد أخطاء من عنّا (لما C++ بتوقف البرنامج بلا استثناء ويندوز)
    constexpr DWORD CODE_TERMINATE = 0xE0000001;   // استثناء C++ ما انمسك
    constexpr DWORD CODE_PURECALL  = 0xE0000002;   // نداء دالة افتراضية خالصة
    constexpr DWORD CODE_INVALID   = 0xE0000003;   // باراميتر غلط لدالة من مكتبة C
    constexpr DWORD CODE_ABORT     = 0xE0000004;   // abort()
    constexpr DWORD CODE_TEST      = 0xE0000005;   // --test-crash
    constexpr int   KEEP_REPORTS   = 10;

    wchar_t       s_dir[MAX_PATH] = {};
    wchar_t       s_version[64]   = {};
    volatile LONG s_busy = 0;

    struct Job { EXCEPTION_POINTERS* ep; DWORD threadId; };

    const char* Describe(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:      return "Access violation";
        case EXCEPTION_STACK_OVERFLOW:        return "Stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "Integer divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "Illegal instruction";
        case EXCEPTION_IN_PAGE_ERROR:         return "In-page error";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "Array bounds exceeded";
        case EXCEPTION_PRIV_INSTRUCTION:      return "Privileged instruction";
        case EXCEPTION_BREAKPOINT:            return "Breakpoint";
        case 0xC0000409:                      return "Stack buffer overrun / fail-fast";
        case 0xE06D7363:                      return "C++ exception (uncaught)";
        case CODE_TERMINATE:                  return "std::terminate (uncaught)";
        case CODE_PURECALL:                   return "Pure virtual function call";
        case CODE_INVALID:                    return "Invalid parameter (CRT)";
        case CODE_ABORT:                      return "abort()";
        case CODE_TEST:                       return "Test crash (--test-crash)";
        }
        return "Unknown exception";
    }

    // "Screen Recorder.exe+0x1A2B3"
    void ModuleOffset(DWORD64 addr, char* out, size_t n)
    {
        HMODULE mod = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)addr, &mod) && mod)
        {
            char path[MAX_PATH] = {};
            GetModuleFileNameA(mod, path, MAX_PATH);
            const char* name = strrchr(path, '\\');
            sprintf_s(out, n, "%s+0x%llX", name ? name + 1 : path, (unsigned long long)(addr - (DWORD64)mod));
        }
        else
            sprintf_s(out, n, "0x%llX", (unsigned long long)addr);
    }

    void WindowsVersion(char* out, size_t n)
    {
        // RtlGetVersion بيعطي الرقم الحقيقي (GetVersionEx بيكذب)
        using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
        OSVERSIONINFOW v{ sizeof(v) };
        if (auto fn = (Fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"); fn && fn(&v) == 0)
            sprintf_s(out, n, "Windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
        else
            strcpy_s(out, n, "Windows (unknown)");
    }

    void WriteStack(FILE* f, const EXCEPTION_POINTERS* ep, DWORD threadId)
    {
        HANDLE proc = GetCurrentProcess();
        HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, threadId);
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        const bool sym = SymInitialize(proc, nullptr, TRUE) != FALSE;

        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset    = ctx.Rip; frame.AddrPC.Mode    = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;

        fprintf(f, "\nStack:\n");
        for (int i = 0; i < 48; ++i)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread ? thread : GetCurrentThread(), &frame, &ctx, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || frame.AddrPC.Offset == 0)
                break;
            char where[MAX_PATH + 32];
            ModuleOffset(frame.AddrPC.Offset, where, sizeof(where));
            fprintf(f, "  #%02d  %s", i, where);
            if (sym)
            {
                alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256] = {};
                auto* info = (SYMBOL_INFO*)buf;
                info->SizeOfStruct = sizeof(SYMBOL_INFO);
                info->MaxNameLen   = 255;
                DWORD64 disp = 0;
                if (SymFromAddr(proc, frame.AddrPC.Offset, &disp, info))
                    fprintf(f, "  %s", info->Name);
                IMAGEHLP_LINE64 line{ sizeof(line) };
                DWORD ldisp = 0;
                if (SymGetLineFromAddr64(proc, frame.AddrPC.Offset, &ldisp, &line))
                {
                    const char* file = strrchr(line.FileName, '\\');
                    fprintf(f, "  (%s:%lu)", file ? file + 1 : line.FileName, line.LineNumber);
                }
            }
            fprintf(f, "\n");
        }
        if (sym) SymCleanup(proc);
        if (thread) CloseHandle(thread);
    }

    // منخلّي آخر KEEP_REPORTS تقارير بس
    void Prune()
    {
        std::vector<std::wstring> names;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((std::wstring(s_dir) + L"\\crash_*.txt").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return;
        do names.push_back(fd.cFileName); while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(names.begin(), names.end());           // الاسم فيه التاريخ: الترتيب = الأقدم أول
        for (size_t i = 0; i + KEEP_REPORTS < names.size(); ++i)
        {
            const std::wstring base = std::wstring(s_dir) + L"\\" + names[i].substr(0, names[i].size() - 4);
            DeleteFileW((base + L".txt").c_str());
            DeleteFileW((base + L".dmp").c_str());
        }
    }

    DWORD WINAPI Writer(LPVOID param)
    {
        const Job& job = *(const Job*)param;
        const EXCEPTION_RECORD& er = *job.ep->ExceptionRecord;

        CreateDirectoryW(s_dir, nullptr);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t base[MAX_PATH];
        swprintf_s(base, L"%s\\crash_%04d-%02d-%02d_%02d-%02d-%02d", s_dir,
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        // ---- .dmp ----
        const std::wstring dmp = std::wstring(base) + L".dmp";
        HANDLE file = CreateFileW(dmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei{ job.threadId, job.ep, FALSE };
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                              (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                              MiniDumpWithUnloadedModules),
                              &mei, nullptr, nullptr);
            CloseHandle(file);
        }

        // ---- .txt ----
        const std::wstring txt = std::wstring(base) + L".txt";
        FILE* f = nullptr;
        if (_wfopen_s(&f, txt.c_str(), L"wb") == 0 && f)
        {
            fputs("\xEF\xBB\xBF", f);              // UTF-8 (مسار الملفات ممكن يكون فيه عربي)
            char where[MAX_PATH + 32], win[96], ver[64];
            WideCharToMultiByte(CP_UTF8, 0, s_version, -1, ver, sizeof(ver), nullptr, nullptr);
            ModuleOffset((DWORD64)er.ExceptionAddress, where, sizeof(where));
            WindowsVersion(win, sizeof(win));
            fprintf(f, "Screen Recorder %s - crash report\n", ver);
            fprintf(f, "Time:      %04d-%02d-%02d %02d:%02d:%02d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            fprintf(f, "System:    %s\n", win);
            fprintf(f, "Exception: 0x%08lX  %s\n", er.ExceptionCode, Describe(er.ExceptionCode));
            fprintf(f, "Address:   %s\n", where);
            if (er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er.NumberParameters >= 2)
                fprintf(f, "Detail:    %s at 0x%llX\n",
                        er.ExceptionInformation[0] == 0 ? "read" : er.ExceptionInformation[0] == 1 ? "write" : "execute",
                        (unsigned long long)er.ExceptionInformation[1]);
            fprintf(f, "Thread:    %lu\n", job.threadId);
            WriteStack(f, job.ep, job.threadId);
            fprintf(f, "\nDump: same name, .dmp (open in Visual Studio: File > Open > File, then Debug with Native Only)\n");
            fclose(f);
        }

        // ---- علامة للتشغيل الجاي ----
        HANDLE mark = CreateFileW((std::wstring(s_dir) + L"\\pending.txt").c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (mark != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(mark, txt.c_str(), (DWORD)(txt.size() * sizeof(wchar_t)), &written, nullptr);
            CloseHandle(mark);
        }
        Prune();
        return 0;
    }

    LONG WINAPI Filter(EXCEPTION_POINTERS* ep)
    {
        if (InterlockedExchange(&s_busy, 1))        // وقع وهو عم يكتب التقرير: منطلع
            return EXCEPTION_EXECUTE_HANDLER;
        Job job{ ep, GetCurrentThreadId() };
        // خيط جديد بمكدّس نضيف (الخيط اللي وقع ممكن يكون مكدّسه خالص)
        if (HANDLE t = CreateThread(nullptr, 256 * 1024, Writer, &job, 0, nullptr))
        {
            WaitForSingleObject(t, 30'000);
            CloseHandle(t);
        }
        return EXCEPTION_EXECUTE_HANDLER;          // منسكّر البرنامج بهدوء (بلا نافذة ويندوز)
    }

    [[noreturn]] void Raise(DWORD code)
    {
        RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        TerminateProcess(GetCurrentProcess(), code);   // ما بيوصل لهون
        for (;;) {}
    }
    void OnPurecall()                                { Raise(CODE_PURECALL); }
    void OnInvalid(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { Raise(CODE_INVALID); }
    void OnAbort(int)                                { Raise(CODE_ABORT); }
}

std::wstring CrashFolder()
{
    if (!s_dir[0])
    {
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        {
            swprintf_s(s_dir, L"%s\\ScreenRecorder\\Crashes", local);
            CoTaskMemFree(local);
        }
    }
    return s_dir;
}

void InstallCrashHandler(const wchar_t* appVersion)
{
    wcsncpy_s(s_version, appVersion ? appVersion : L"?", _TRUNCATE);
    const std::wstring dir = CrashFolder();
    const size_t slash = dir.find_last_of(L'\\');
    if (slash != std::wstring::npos)
        CreateDirectoryW(dir.substr(0, slash).c_str(), nullptr);   // ScreenRecorder (إذا أول تشغيل)

    SetUnhandledExceptionFilter(Filter);
    std::set_terminate([] { Raise(CODE_TERMINATE); });
    _set_purecall_handler(OnPurecall);
    _set_invalid_parameter_handler(OnInvalid);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    signal(SIGABRT, OnAbort);
}

bool TakePendingCrash(std::wstring& reportPath)
{
    const std::wstring mark = CrashFolder() + L"\\pending.txt";
    HANDLE h = CreateFileW(mark.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD read = 0;
    ReadFile(h, buf, sizeof(buf) - sizeof(wchar_t), &read, nullptr);
    CloseHandle(h);
    DeleteFileW(mark.c_str());
    reportPath.assign(buf, read / sizeof(wchar_t));
    return true;
}

void TestCrash()
{
    Raise(CODE_TEST);
}
