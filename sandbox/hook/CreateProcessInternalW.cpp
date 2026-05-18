#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include "utils/Log.hpp"
#include "__init__.hpp"
#include "Sandbox.hpp"
#include "CreateProcessInternalW.hpp"

T_CreateProcessInternalW sys_CreateProcessInternalW = nullptr;

static BOOL Hook_CreateProcessInternalW(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                        LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                        LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                        ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                        LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation,
                                        PHANDLE hNewToken)
{
    PROCESS_INFORMATION backup;
    if (lpProcessInformation == nullptr)
    {
        lpProcessInformation = &backup;
        ZeroMemory(&backup, sizeof(backup));
    }

    /* Create process */
    if (!sys_CreateProcessInternalW(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
                                    bInheritHandles, dwCreationFlags | CREATE_SUSPENDED, lpEnvironment,
                                    lpCurrentDirectory, lpStartupInfo, lpProcessInformation, hNewToken))
    {
        return FALSE;
    }

#if defined(_WIN64)
    LPCSTR lpDllName = appbox::sandbox->inject_data.sandbox64_path.c_str();
#else
    LPCSTR lpDllName = appbox::sandbox->inject_data.sandbox32_path.c_str();
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

void appbox::InjectCreateProcessInternalW()
{
    auto addr = GetProcAddress(sys.h_kernelbase, "CreateProcessInternalW");
    if (addr == nullptr)
    {
        addr = GetProcAddress(sys.h_kernel32, "CreateProcessInternalW");
    }

    if (addr == nullptr)
    {
        THROW_LOG("hook CreateProcessInternalW failed");
    }
    sys_CreateProcessInternalW = reinterpret_cast<T_CreateProcessInternalW>(addr);

    auto ret = DetourAttach(&sys_CreateProcessInternalW, Hook_CreateProcessInternalW);
    if (ret != NO_ERROR)
    {
        THROW_LOG("DetourAttach(CreateProcessInternalW) failed");
    }
}
