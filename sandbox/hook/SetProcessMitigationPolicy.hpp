#ifndef APPBOX_SANDBOX_HOOK_SETPROCESSMITIGATIONPOLICY_HPP
#define APPBOX_SANDBOX_HOOK_SETPROCESSMITIGATIONPOLICY_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see
 * https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessmitigationpolicy
 */
/* clang-format off */
typedef BOOL (*T_SetProcessMitigationPolicy)(
    /* [IN] */  PROCESS_MITIGATION_POLICY   MitigationPolicy,
    /* [IN] */  PVOID                       lpBuffer,
    /* [IN] */  SIZE_T                      dwLength
);
/* clang-format on */

/**
 * @brief SetProcessMitigationPolicy() direct call.
 */
extern T_SetProcessMitigationPolicy sys_SetProcessMitigationPolicy;
}

namespace appbox
{

/**
 * @brief Inject SetProcessMitigationPolicy() hook.
 */
void AttachSetProcessMitigationPolicy();
void DetachSetProcessMitigationPolicy();

} // namespace appbox

#endif
