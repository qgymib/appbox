#ifndef APPBOX_SANDBOX_HOOK_DNSQUERY_W_HPP
#define APPBOX_SANDBOX_HOOK_DNSQUERY_W_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsquery_w
 */
/* clang-format off */
typedef LONG(WSAAPI* T_DnsQuery_W)(
    /* [IN] */           PCWSTR  Name,
    /* [IN] */           WORD    Type,
    /* [IN] */           DWORD   Options,
    /* [IN,OPTIONAL] */  PVOID   Extra,
    /* [OUT] */          PVOID*  Results,
    /* [IN,OUT,OPT] */   PVOID*  Reserved
);
/* clang-format on */

/**
 * @brief DnsQuery_W() direct call.
 */
extern T_DnsQuery_W sys_DnsQuery_W;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook DnsQuery_W().
 */
extern HookRecord HookDnsQueryW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_DNSQUERY_W_HPP
