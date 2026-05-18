#include "utils/WinAPI.h" /* Must be first include file */
#include <detours.h>
#include "__init__.hpp"
#include "SetProcessMitigationPolicy.hpp"

T_SetProcessMitigationPolicy sys_SetProcessMitigationPolicy = nullptr;

static BOOL Hook_SetProcessMitigationPolicy(PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength)
{
    if (MitigationPolicy == ProcessDynamicCodePolicy)
    {
        return TRUE;
    }
    return sys_SetProcessMitigationPolicy(MitigationPolicy, lpBuffer, dwLength);
}

void appbox::InjectSetProcessMitigationPolicy()
{
    auto addr = GetProcAddress(sys.h_kernelbase, "SetProcessMitigationPolicy");
    sys_SetProcessMitigationPolicy = reinterpret_cast<T_SetProcessMitigationPolicy>(addr);

    /* Windows 8 */
    if (appbox::sys.OSBuild >= 8400)
    {
        DetourAttach(&sys_SetProcessMitigationPolicy, Hook_SetProcessMitigationPolicy);
    }
}
