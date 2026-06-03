#include "utils/WinAPI.h" /* Must be first include file */
#include "LdrQueryImageFileExecutionOptionsEx.hpp"

T_LdrQueryImageFileExecutionOptionsEx sys_LdrQueryImageFileExecutionOptionsEx = nullptr;

static NTSTATUS Hook_LdrQueryImageFileExecutionOptionsEx(PUNICODE_STRING lpImageFile, PCWSTR lpszOption, ULONG dwType,
                                                         PVOID lpData, ULONG cbData, ULONG* lpcbData, BOOLEAN bWow64)
{
    if (appbox::sys.OSBuild < 14942)
    {
        return sys_LdrQueryImageFileExecutionOptionsEx(lpImageFile, lpszOption, dwType, lpData, cbData, lpcbData,
                                                       bWow64);
    }

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

    return sys_LdrQueryImageFileExecutionOptionsEx(lpImageFile, lpszOption, dwType, lpData, cbData, lpcbData, bWow64);
}

static void LoadLdrQueryImageFileExecutionOptionsEx()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "LdrQueryImageFileExecutionOptionsEx");
    sys_LdrQueryImageFileExecutionOptionsEx = reinterpret_cast<T_LdrQueryImageFileExecutionOptionsEx>(addr);
}

appbox::HookRecord appbox::HookLdrQueryImageFileExecutionOptionsEx = {
    "LdrQueryImageFileExecutionOptionsEx",
    LoadLdrQueryImageFileExecutionOptionsEx,
    (void**)&sys_LdrQueryImageFileExecutionOptionsEx,
    Hook_LdrQueryImageFileExecutionOptionsEx,
};
