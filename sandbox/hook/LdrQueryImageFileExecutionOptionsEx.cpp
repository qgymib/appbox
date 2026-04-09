#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_NO_STATUS
#include <windows.h>
#include <detours.h>
#include "__init__.hpp"

static NTSTATUS Hook_LdrQueryImageFileExecutionOptionsEx(PUNICODE_STRING lpImageFile,
                                                         PCWSTR lpszOption, ULONG dwType,
                                                         PVOID lpData, ULONG cbData,
                                                         ULONG* lpcbData, BOOLEAN bWow64)
{
    /*
     * Sandbox on ARM64 requires x86 applications NOT to use the CHPE binaries.
     *
     * This hook causes CreateProcessInternalW to set PsAttributeChpe = 0 which
     * makes the kernel load the regular non hybrid version of ntdll into the new process.
     */
    if (_wcsicmp(lpszOption, L"LoadCHPEBinaries") == 0)
    {
        *static_cast<ULONG*>(lpData) = 0;
        return 0; /* STATUS_SUCCESS */
    }

    return appbox::sys.LdrQueryImageFileExecutionOptionsEx(lpImageFile, lpszOption, dwType, lpData,
                                                           cbData, lpcbData, bWow64);
}

APPBOX_SANDBOX_INJECT(LdrQueryImageFileExecutionOptionsEx)
{
    APPBOX_GET_PROC(sys.h_ntdll, LdrQueryImageFileExecutionOptionsEx);

    /* Windows 10 */
    if (appbox::sys.OSBuild >= 14942)
    {
        DetourAttach(&appbox::sys.LdrQueryImageFileExecutionOptionsEx,
                     Hook_LdrQueryImageFileExecutionOptionsEx);
    }
}
