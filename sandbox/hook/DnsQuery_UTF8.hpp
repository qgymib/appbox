#ifndef APPBOX_SANDBOX_HOOK_DNSQUERY_UTF8_HPP
#define APPBOX_SANDBOX_HOOK_DNSQUERY_UTF8_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsquery_utf8
 */
/* clang-format off */
typedef LONG(WSAAPI* T_DnsQuery_UTF8)(
    /* [IN] */           PCSTR   Name,
    /* [IN] */           WORD    Type,
    /* [IN] */           DWORD   Options,
    /* [IN,OPTIONAL] */  PVOID   Extra,
    /* [OUT] */          PVOID*  Results,
    /* [IN,OUT,OPT] */   PVOID*  Reserved
);
/* clang-format on */

/**
 * @brief DnsQuery_UTF8() direct call.
 */
extern T_DnsQuery_UTF8 sys_DnsQuery_UTF8;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook DnsQuery_UTF8().
 */
extern HookRecord HookDnsQueryUTF8;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_DNSQUERY_UTF8_HPP
