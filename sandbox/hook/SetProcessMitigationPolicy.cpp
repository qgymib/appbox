#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <detours.h>
#include "__init__.hpp"

static BOOL Hook_SetProcessMitigationPolicy(PROCESS_MITIGATION_POLICY MitigationPolicy,
                                            PVOID lpBuffer, SIZE_T dwLength)
{
    if (MitigationPolicy == ProcessDynamicCodePolicy)
    {
        return TRUE;
    }
    return appbox::sys.SetProcessMitigationPolicy(MitigationPolicy, lpBuffer, dwLength);
}

APPBOX_SANDBOX_INJECT(SetProcessMitigationPolicy)
{
    APPBOX_GET_PROC(sys.h_kernelbase, LdrQueryImageFileExecutionOptionsEx);

    /* Windows 8 */
    if (appbox::sys.OSBuild >= 8400)
    {
        DetourAttach(&sys.SetProcessMitigationPolicy, Hook_SetProcessMitigationPolicy);
    }
}
