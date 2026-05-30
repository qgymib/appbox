#ifndef APPBOX_SANDBOX_HOOK_CREATEPROCESSINTERNALW_HPP
#define APPBOX_SANDBOX_HOOK_CREATEPROCESSINTERNALW_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see http://a-twisted-world.blogspot.com/2008/03/createprocessinternal-function.html
 */
/* clang-format off */
typedef BOOL (*T_CreateProcessInternalW)(
    /* [IN] */              HANDLE                  hToken,
    /* [IN,OPTIONAL]*/      LPCWSTR                 lpApplicationName,
    /* [IN,OUT,OPTIONAL] */ LPWSTR                  lpCommandLine,
    /* [IN,OPTIONAL] */     LPSECURITY_ATTRIBUTES   lpProcessAttributes,
    /* [IN,OPTIONAL] */     LPSECURITY_ATTRIBUTES   lpThreadAttributes,
    /* [IN] */              BOOL                    bInheritHandles,
    /* [IN] */              ULONG                   dwCreationFlags,
    /* [IN,OPTIONAL] */     LPVOID                  lpEnvironment,
    /* [IN,OPTIONAL] */     LPCWSTR                 lpCurrentDirectory,
    /* [IN] */              LPSTARTUPINFOW          lpStartupInfo,
    /* [OUT] */             LPPROCESS_INFORMATION   lpProcessInformation,
    /* [IN] */              PHANDLE                 hNewToken
);
/* clang-format on */

/**
 * @brief CreateProcessInternalW() direct call.
 */
extern T_CreateProcessInternalW sys_CreateProcessInternalW;
}

namespace appbox
{

/**
 * @brief Inject CreateProcessInternalW() hook.
 */
void AttachCreateProcessInternalW();
void DetachCreateProcessInternalW();

} // namespace appbox

#endif
