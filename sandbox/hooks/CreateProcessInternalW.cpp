#include "utils/winapi.hpp"
#include "__init__.hpp"
#include "appbox.hpp"
#include "config.hpp"
#include <detours.h>
#include <spdlog/spdlog.h>

struct HookCreateProcessInternalCtx
{
    std::string      injectData;   /* Inject data. */
    CRITICAL_SECTION lock;         /* Global lock for arguments sync. */
    HANDLE           hTmpToken;    /* first argument for CreateProcessInternalW() */
    HANDLE*          hTmpNewToken; /* Last argument for CreateProcessInternalW() */
};

static HookCreateProcessInternalCtx* hook_CreateProcessInternalCtx = nullptr;

static BOOL WINAPI Hook_CreateProcessInternalW_Proxy(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags,
    LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation)
{
    HANDLE  hToken = hook_CreateProcessInternalCtx->hTmpToken;
    HANDLE* hNewToken = hook_CreateProcessInternalCtx->hTmpNewToken;
    auto    sys_CreateProcessInternalW =
        static_cast<appbox::winapi::CreateProcessInternalW>(appbox::CreateProcessInternalW.orig);

    return sys_CreateProcessInternalW(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes,
                                      lpThreadAttributes, bInheritHandles, dwCreationFlags,
                                      lpEnvironment, lpCurrentDirectory, lpStartupInfo,
                                      lpProcessInformation, hNewToken);
}

static BOOL CALLBACK Hook_CreateProcessInternalW_Init(PINIT_ONCE, PVOID, PVOID*)
{
    hook_CreateProcessInternalCtx = new HookCreateProcessInternalCtx;
    InitializeCriticalSection(&hook_CreateProcessInternalCtx->lock);

    nlohmann::json injectJson = appbox::G->config;
    hook_CreateProcessInternalCtx->injectData = injectJson.dump();

    return TRUE;
}

static BOOL Hook_CreateProcessInternalW(
    HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation, PHANDLE hNewToken)
{
    static INIT_ONCE once_token = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once_token, Hook_CreateProcessInternalW_Init, nullptr, nullptr);

#if defined(_WIN64)
    const std::string& tempDllPath = appbox::G->config.dllPath64;
#else
    const std::string& tempDllPath = appbox::G->config.dllPath32;
#endif

    BOOL result;
    EnterCriticalSection(&hook_CreateProcessInternalCtx->lock);
    do
    {
        /* Sync external arguments. */
        hook_CreateProcessInternalCtx->hTmpToken = hToken;
        hook_CreateProcessInternalCtx->hTmpNewToken = hNewToken;

        /* Create the process in suspended state. */
        result = DetourCreateProcessWithDllExW(
            lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
            bInheritHandles, dwCreationFlags | CREATE_SUSPENDED, lpEnvironment, lpCurrentDirectory,
            lpStartupInfo, lpProcessInformation, tempDllPath.c_str(),
            Hook_CreateProcessInternalW_Proxy);
        if (!result)
        {
            break;
        }

        /* Inject data. */
        const GUID  guid = APPBOX_SANDBOX_GUID;
        const void* injectData = hook_CreateProcessInternalCtx->injectData.c_str();
        DWORD injectDataSize = static_cast<DWORD>(hook_CreateProcessInternalCtx->injectData.size());
        result = DetourCopyPayloadToProcess(lpProcessInformation->hProcess, guid, injectData,
                                            injectDataSize);
        if (!NT_SUCCESS(result))
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
    LeaveCriticalSection(&hook_CreateProcessInternalCtx->lock);

    return result;
}

appbox::Detour appbox::CreateProcessInternalW = {
    "CreateProcessInternalW",
    L"KernelBase.dll",
    Hook_CreateProcessInternalW,
    nullptr,
};
