#ifndef APPBOX_SANDBOX_HOOK_DNSQUERY_A_HPP
#define APPBOX_SANDBOX_HOOK_DNSQUERY_A_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsquery_a
 */
/* clang-format off */
typedef LONG(WSAAPI* T_DnsQuery_A)(
    /* [IN] */           PCSTR   Name,
    /* [IN] */           WORD    Type,
    /* [IN] */           DWORD   Options,
    /* [IN,OPTIONAL] */  PVOID   Extra,
    /* [OUT] */          PVOID*  Results,
    /* [IN,OUT,OPT] */   PVOID*  Reserved
);
/* clang-format on */

/**
 * @brief DnsQuery_A() direct call.
 */
extern T_DnsQuery_A sys_DnsQuery_A;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook DnsQuery_A().
 */
extern HookRecord HookDnsQueryA;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_DNSQUERY_A_HPP
