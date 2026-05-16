#ifndef APPBOX_SANDBOX_HOOK_SETPROCESSMITIGATIONPOLICY_HPP
#define APPBOX_SANDBOX_HOOK_SETPROCESSMITIGATIONPOLICY_HPP

#include "utils/WinAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @see
 * https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessmitigationpolicy
 */
typedef BOOL (*T_SetProcessMitigationPolicy)(PROCESS_MITIGATION_POLICY MitigationPolicy,
                                           PVOID lpBuffer, SIZE_T dwLength);

extern T_SetProcessMitigationPolicy sys_SetProcessMitigationPolicy;

#ifdef __cplusplus
}
#endif

namespace appbox
{

void InjectSetProcessMitigationPolicy();

}

#endif
