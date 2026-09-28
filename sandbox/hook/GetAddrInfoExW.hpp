#ifndef APPBOX_SANDBOX_HOOK_GETADDRINFOEXW_HPP
#define APPBOX_SANDBOX_HOOK_GETADDRINFOEXW_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfoexw
 */
/* clang-format off */
typedef INT(WSAAPI* T_GetAddrInfoExW)(
    /* [IN,OPTIONAL] */  PCWSTR              NodeName,
    /* [IN,OPTIONAL] */  PCWSTR              ServiceName,
    /* [IN] */           DWORD               NameSpace,
    /* [IN,OPTIONAL] */  LPGUID              Provider,
    /* [IN,OPTIONAL] */  const ADDRINFOEXW*  Hints,
    /* [OUT] */          PADDRINFOEXW*       Result,
    /* [IN,OPTIONAL] */  struct timeval*     Timeout,
    /* [IN,OPTIONAL] */  LPOVERLAPPED        Overlapped,
    /* [IN,OPTIONAL] */  PVOID               CompletionRoutine,
    /* [IN,OUT,OPT] */   LPHANDLE            NameHandle
);
/* clang-format on */

/**
 * @brief GetAddrInfoExW() direct call.
 */
extern T_GetAddrInfoExW sys_GetAddrInfoExW;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook GetAddrInfoExW().
 */
extern HookRecord HookGetAddrInfoExW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETADDRINFOEXW_HPP
