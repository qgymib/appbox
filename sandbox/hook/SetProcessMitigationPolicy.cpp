#include "utils/WinAPI.h" /* Must be first include file */
#include "SetProcessMitigationPolicy.hpp"

T_SetProcessMitigationPolicy sys_SetProcessMitigationPolicy = nullptr;

static BOOL Hook_SetProcessMitigationPolicy(PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength)
{
    /* Windows 8 */
    if (appbox::sys.OSBuild < 8400)
    {
        return sys_SetProcessMitigationPolicy(MitigationPolicy, lpBuffer, dwLength);
    }

    if (MitigationPolicy == ProcessDynamicCodePolicy)
    {
        return TRUE;
    }
    return sys_SetProcessMitigationPolicy(MitigationPolicy, lpBuffer, dwLength);
}

static void LoadSetProcessMitigationPolicy()
{
    auto addr = GetProcAddress(appbox::sys.h_kernelbase, "SetProcessMitigationPolicy");
    sys_SetProcessMitigationPolicy = reinterpret_cast<T_SetProcessMitigationPolicy>(addr);
}

appbox::HookRecord appbox::HookSetProcessMitigationPolicy = {
    "SetProcessMitigationPolicy",
    LoadSetProcessMitigationPolicy,
    (void**)&sys_SetProcessMitigationPolicy,
    Hook_SetProcessMitigationPolicy,
};
