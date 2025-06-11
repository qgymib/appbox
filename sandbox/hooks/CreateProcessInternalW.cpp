#include "utils/winapi.hpp"
#include "utils/wstring.hpp"
#include "__init__.hpp"
#include "appbox.hpp"
#include "config.hpp"
#include <detours.h>
#include <spdlog/spdlog.h>

struct HookCreateProcessInternalCtx
{
    HookCreateProcessInternalCtx();
    ~HookCreateProcessInternalCtx();

    std::string      injectData;   /* Inject data. */
    CRITICAL_SECTION lock;         /* Global lock for arguments sync. */
    HANDLE           hTmpToken;    /* first argument for CreateProcessInternalW() */
    HANDLE*          hTmpNewToken; /* Last argument for CreateProcessInternalW() */
};

static appbox::Instance<HookCreateProcessInternalCtx> ctx_CreateProcessInternal;

HookCreateProcessInternalCtx::HookCreateProcessInternalCtx()
{
    InitializeCriticalSection(&lock);
    hTmpToken = INVALID_HANDLE_VALUE;
    hTmpNewToken = nullptr;

    nlohmann::json injectJson = appbox::G->config;
    injectData = injectJson.dump();
}

HookCreateProcessInternalCtx::~HookCreateProcessInternalCtx()
{
    DeleteCriticalSection(&lock);
}

static BOOL WINAPI Hook_CreateProcessInternalW_Proxy(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags,
    LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation)
{
    HANDLE  hToken = ctx_CreateProcessInternal->hTmpToken;
    HANDLE* hNewToken = ctx_CreateProcessInternal->hTmpNewToken;
    auto    sys_CreateProcessInternalW =
        static_cast<appbox::winapi::CreateProcessInternalW>(appbox::CreateProcessInternalW.orig);

    return sys_CreateProcessInternalW(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes,
                                      lpThreadAttributes, bInheritHandles, dwCreationFlags,
                                      lpEnvironment, lpCurrentDirectory, lpStartupInfo,
                                      lpProcessInformation, hNewToken);
}

static BOOL Hook_CreateProcessInternalW_InSandbox(
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

    BOOL result;
    EnterCriticalSection(&ctx_CreateProcessInternal->lock);
    do
    {
        /* Sync external arguments. */
        ctx_CreateProcessInternal->hTmpToken = hToken;
        ctx_CreateProcessInternal->hTmpNewToken = hNewToken;

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
        const void* injectData = ctx_CreateProcessInternal->injectData.c_str();
        DWORD injectDataSize = static_cast<DWORD>(ctx_CreateProcessInternal->injectData.size());
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
    LeaveCriticalSection(&ctx_CreateProcessInternal->lock);

    return result;
}

/**
 * @brief Convert \p path to real host path.
 * @param[in] path File path in the sandbox or host file system.
 * @return File path in host file system.
 */
static std::wstring ToHostPath(const std::wstring& path)
{
    /* Get full path of target path. */
    DWORD                                ret;
    std::array<wchar_t, APPBOX_MAX_PATH> buff;
    ret = GetFullPathNameW(path.c_str(), buff.size(), buff.data(), nullptr);
    if (ret == 0 || ret == buff.size())
    {
        spdlog::error("GetFullPathNameW() failed");
        abort();
    }

    /*
     * The `fullPath` can have following conditions:
     * 1. Path like `C:\foo\bar` if `path` is absolute path.
     * 2. Path like `PathToSandbox\C\foo\bar` if `path` is relative path.
     */
    std::wstring wFullPath(buff.data(), ret);

    /* If it is already real host path. */
    if (appbox::StartWith(wFullPath, appbox::G->wSandboxPath))
    {
        return wFullPath;
    }

    /*
     * + `C:\fo`
     * + `C:\foo` <-- lower_bound
     * + `C:\foo\a` <-- key
     * + `C:\foo\bar`
     */
    auto it = appbox::G->vFileTable.lower_bound(wFullPath);
    if (it == appbox::G->vFileTable.end())
    {
        return wFullPath;
    }
    if (appbox::StartWith(wFullPath, it->first))
    {
        if (wFullPath[it->first.size()] == L'\\')
        {

        }
    }
}

/**
 * @brief Fix application path.
 * @param[in,out] wApplicationName Application name or path.
 * @param[in,out] wCommandLine Application command line arguments.
 * @return true if run in sandbox, false if escape.
 */
static bool Hook_CreateProcessInternalW_FixPath(std::wstring& wApplicationName,
                                                std::wstring& wCommandLine)
{
}

static BOOL Hook_CreateProcessInternalW(
    HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation, PHANDLE hNewToken)
{
    /* Fix the application path if it is in the sandbox. */
    std::wstring wApplicationName = lpApplicationName != nullptr ? lpApplicationName : L"";
    std::wstring wCommandLine = lpCommandLine != nullptr ? lpCommandLine : L"";
    const bool   inSandbox = Hook_CreateProcessInternalW_FixPath(wApplicationName, wCommandLine);
    lpApplicationName = wApplicationName.empty() ? nullptr : &wApplicationName[0];
    lpCommandLine = wCommandLine.empty() ? nullptr : &wCommandLine[0];

    /* The process should not run in the sandbox. */
    if (inSandbox)
    {
        return Hook_CreateProcessInternalW_InSandbox(
            hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
            bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo,
            lpProcessInformation, hNewToken);
    }

    auto sys_CreateProcessInternalW =
        static_cast<appbox::winapi::CreateProcessInternalW>(appbox::CreateProcessInternalW.orig);
    return sys_CreateProcessInternalW(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes,
                                      lpThreadAttributes, bInheritHandles, dwCreationFlags,
                                      lpEnvironment, lpCurrentDirectory, lpStartupInfo,
                                      lpProcessInformation, hNewToken);
}

appbox::Detour appbox::CreateProcessInternalW = {
    "CreateProcessInternalW",
    L"KernelBase.dll",
    Hook_CreateProcessInternalW,
    nullptr,
};
