#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <detours.h>
#include <spdlog/spdlog.h>
#include "__init__.hpp"
#include "Sandbox.hpp"

static BOOL Hook_CreateProcessInternalW(
    HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation, PHANDLE hNewToken)
{
    PROCESS_INFORMATION backup;
    if (lpProcessInformation == nullptr)
    {
        lpProcessInformation = &backup;
        ZeroMemory(&backup, sizeof(backup));
    }

    /* Create process */
    if (!appbox::sys.CreateProcessInternalW(
            hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
            bInheritHandles, dwCreationFlags | CREATE_SUSPENDED, lpEnvironment, lpCurrentDirectory,
            lpStartupInfo, lpProcessInformation, hNewToken))
    {
        return FALSE;
    }

#if defined(_WIN64)
    LPCSTR lpDllName = appbox::g_sandbox->inject_data.sandbox64_path.c_str();
#else
    LPCSTR lpDllName = appbox::g_sandbox->inject_data.sandbox32_path.c_str();
#endif

    /* Inject DLL */
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

APPBOX_SANDBOX_INJECT(CreateProcessInternalW)
{
    APPBOX_GET_PROC(sys.h_kernelbase, CreateProcessInternalW);
    if (sys.CreateProcessInternalW == nullptr)
    {
        APPBOX_GET_PROC(sys.h_kernel32, CreateProcessInternalW);
    }

    if (sys.CreateProcessInternalW == nullptr)
    {
        SPDLOG_ERROR("hook CreateProcessInternalW failed");
        throw std::runtime_error("hook CreateProcessInternalW failed");
    }

    DetourAttach(&appbox::sys.CreateProcessInternalW, Hook_CreateProcessInternalW);
}
