#ifndef APPBOX_SANDBOX_HOOK_GETHOSTBYNAME_HPP
#define APPBOX_SANDBOX_HOOK_GETHOSTBYNAME_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-gethostbyname
 */
typedef struct hostent*(WSAAPI* T_gethostbyname)(PCSTR Name);

/**
 * @brief gethostbyname() direct call.
 */
extern T_gethostbyname sys_gethostbyname;

} // extern "C"

namespace appbox
{

/**
 * @brief Hook gethostbyname().
 */
extern HookRecord HookGetHostByName;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_GETHOSTBYNAME_HPP
