#include "utils/winapi.hpp"
#include "__init__.hpp"
#include "appbox.hpp"
#include "config.hpp"
#include <detours.h>
#include <spdlog/spdlog.h>

struct HookCreateProcessInternalW
{
    std::string      injectData;   /* Inject data. */
    CRITICAL_SECTION lock;         /* Global lock for arguments sync. */
    HANDLE           hTmpToken;    /* first argument for CreateProcessInternalW() */
    HANDLE*          hTmpNewToken; /* Last argument for CreateProcessInternalW() */
};

static HookCreateProcessInternalW* hook_CreateProcessInternalW = nullptr;

static BOOL WINAPI s_proxy_CREATE_PROCESS_ROUTINEW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                                   LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                                   LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                                   BOOL bInheritHandles, DWORD dwCreationFlags,
                                                   LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                                   LPSTARTUPINFOW        lpStartupInfo,
                                                   LPPROCESS_INFORMATION lpProcessInformation)
{
    HANDLE                                 hToken = hook_CreateProcessInternalW->hTmpToken;
    HANDLE*                                hNewToken = hook_CreateProcessInternalW->hTmpNewToken;
    appbox::winapi::CreateProcessInternalW fn =
        (appbox::winapi::CreateProcessInternalW)appbox::CreateProcessInternalW.orig;

    return fn(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
              bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo,
              lpProcessInformation, hNewToken);
}

static BOOL s_hook_CreateProcessInternalW_init(PINIT_ONCE, PVOID, PVOID*)
{
    hook_CreateProcessInternalW = new HookCreateProcessInternalW;
    InitializeCriticalSection(&hook_CreateProcessInternalW->lock);

    nlohmann::json injectJson = appbox::G->config;
    hook_CreateProcessInternalW->injectData = injectJson.dump();

    return TRUE;
}

static BOOL s_hook_CreateProcessInternalW(
    HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation, PHANDLE hNewToken)
{
#if defined(_WIN64)
    const std::string& tempDllPath = appbox::G->config.dllPath64;
#else
    const std::string& tempDllPath = appbox::G->config.dllPath32;
#endif

    static INIT_ONCE once_token = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once_token, s_hook_CreateProcessInternalW_init, nullptr, nullptr);

    BOOL result;
    EnterCriticalSection(&hook_CreateProcessInternalW->lock);
    do
    {
        /* Sync external arguments. */
        hook_CreateProcessInternalW->hTmpToken = hToken;
        hook_CreateProcessInternalW->hTmpNewToken = hNewToken;

        /* Create the process in suspended state. */
        result = DetourCreateProcessWithDllExW(
            lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
            bInheritHandles, dwCreationFlags | CREATE_SUSPENDED, lpEnvironment, lpCurrentDirectory,
            lpStartupInfo, lpProcessInformation, tempDllPath.c_str(),
            s_proxy_CREATE_PROCESS_ROUTINEW);
        if (!result)
        {
            break;
        }

        /* Inject data. */
        const GUID guid = APPBOX_SANDBOX_GUID;
        result = DetourCopyPayloadToProcess(lpProcessInformation->hProcess, guid,
                                            hook_CreateProcessInternalW->injectData.c_str(),
                                            (DWORD)hook_CreateProcessInternalW->injectData.size());
        if (!result)
        {
            spdlog::error("DetourCopyPayloadToProcess() failed");
            TerminateProcess(lpProcessInformation->hProcess, EXIT_FAILURE);
            CloseHandle(lpProcessInformation->hThread);
            CloseHandle(lpProcessInformation->hProcess);
            break;
        }

        /* Result thread if user not set `CREATE_SUSPENDED`. */
        if (!(dwCreationFlags & CREATE_SUSPENDED))
        {
            ResumeThread(lpProcessInformation->hThread);
        }
    } while (FALSE);
    LeaveCriticalSection(&hook_CreateProcessInternalW->lock);

    return result;
}

appbox::Detour appbox::CreateProcessInternalW = {
    "CreateProcessInternalW",
    { L"KernelBase.dll", L"kernel32.dll" },
    s_hook_CreateProcessInternalW,
    nullptr,
};
