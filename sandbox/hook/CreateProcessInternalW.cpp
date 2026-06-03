#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "Sandbox.hpp"
#include "CreateProcessInternalW.hpp"
#include "WString.hpp"
#include "Defines.hpp"
#include <detours.h>

T_CreateProcessInternalW sys_CreateProcessInternalW = nullptr;

static nlohmann::json CreateProcessInternalWLogParam(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                                     LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                                     LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                                     ULONG dwCreationFlags, LPVOID lpEnvironment,
                                                     LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
                                                     LPPROCESS_INFORMATION lpProcessInformation, PHANDLE hNewToken)
{
    nlohmann::json param;
    param["hToken"] = appbox::PointerToString(hToken);
    if (lpApplicationName != nullptr)
    {
        param["lpApplicationName"] = appbox::WideToUTF8(lpApplicationName);
    }
    if (lpCommandLine != nullptr)
    {
        param["lpCommandLine"] = appbox::WideToUTF8(lpCommandLine);
    }
    param["lpProcessAttributes"] = appbox::PointerToString(lpProcessAttributes);
    param["lpThreadAttributes"] = appbox::PointerToString(lpThreadAttributes);
    param["bInheritHandles"] = bInheritHandles;
    param["dwCreationFlags"] = dwCreationFlags;
    param["lpEnvironment"] = appbox::PointerToString(lpEnvironment);
    if (lpCurrentDirectory != nullptr)
    {
        param["lpCurrentDirectory"] = appbox::WideToUTF8(lpCurrentDirectory);
    }
    param["lpStartupInfo"] = appbox::PointerToString(lpStartupInfo);
    param["lpProcessInformation"] = appbox::PointerToString(lpProcessInformation);
    param["hNewToken"] = appbox::PointerToString(hNewToken);
    return param;
}
static appbox::LoggerF logger("CreateProcessInternalW", CreateProcessInternalWLogParam);

static BOOL WrapDetourCreateProcessWithDllExW(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                              LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                              LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                              DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                              LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation,
                                              PHANDLE hNewToken, LPCSTR lpDllName)
{
    PROCESS_INFORMATION backup;
    if (lpProcessInformation == NULL)
    {
        lpProcessInformation = &backup;
        ZeroMemory(&backup, sizeof(backup));
    }

    if (!sys_CreateProcessInternalW(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
                                    bInheritHandles, dwCreationFlags | CREATE_SUSPENDED, lpEnvironment,
                                    lpCurrentDirectory, lpStartupInfo, lpProcessInformation, hNewToken))
    {
        return FALSE;
    }

    LPCSTR sz = lpDllName;
    if (!DetourUpdateProcessWithDll(lpProcessInformation->hProcess, &sz, 1) &&
        !DetourProcessViaHelperW(lpProcessInformation->dwProcessId, lpDllName, CreateProcessW))
    {

        TerminateProcess(lpProcessInformation->hProcess, ~0u);
        CloseHandle(lpProcessInformation->hProcess);
        CloseHandle(lpProcessInformation->hThread);
        return FALSE;
    }

    if (!(dwCreationFlags & CREATE_SUSPENDED))
    {
        ResumeThread(lpProcessInformation->hThread);
    }

    if (lpProcessInformation == &backup)
    {
        CloseHandle(lpProcessInformation->hProcess);
        CloseHandle(lpProcessInformation->hThread);
    }
    return TRUE;
}

static BOOL Hook_CreateProcessInternalW(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                        LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                        LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                        ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                        LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation,
                                        PHANDLE hNewToken)
{
    logger.Log(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles,
               dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation, hNewToken);

#if defined(_WIN64)
    LPCSTR lpDllName = appbox::sandbox->sandbox64_dos_path.c_str();
#else
    LPCSTR lpDllName = appbox::sandbox->sandbox32_dos_path.c_str();
#endif

    if (!WrapDetourCreateProcessWithDllExW(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes,
                                           lpThreadAttributes, bInheritHandles, dwCreationFlags | CREATE_SUSPENDED,
                                           lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation,
                                           hNewToken, lpDllName))
    {
        return FALSE;
    }

    const GUID guid = SANDBOX_GUID;
    auto       inject_data_sz = static_cast<DWORD>(appbox::sandbox->inject_data.size());
    if (!DetourCopyPayloadToProcess(lpProcessInformation->hProcess, guid, appbox::sandbox->inject_data.c_str(),
                                    inject_data_sz))
    {
        auto errcode = GetLastError();
        TerminateProcess(lpProcessInformation->hProcess, errcode);
        CloseHandle(lpProcessInformation->hProcess);
        CloseHandle(lpProcessInformation->hThread);
        return FALSE;
    }

    ResumeThread(lpProcessInformation->hThread);
    return TRUE;
}

static void LoadCreateProcessInternalW()
{
    auto addr = GetProcAddress(appbox::sys.h_kernelbase, "CreateProcessInternalW");
    if (addr == nullptr)
    {
        addr = GetProcAddress(appbox::sys.h_kernel32, "CreateProcessInternalW");
    }

    sys_CreateProcessInternalW = reinterpret_cast<T_CreateProcessInternalW>(addr);
}

appbox::HookRecord appbox::HookCreateProcessInternalW = {
    "CreateProcessInternalW",
    LoadCreateProcessInternalW,
    (void**)&sys_CreateProcessInternalW,
    Hook_CreateProcessInternalW,
};
