#ifndef APPBOX_SANDBOX_HOOK_NTCLOSE_HPP
#define APPBOX_SANDBOX_HOOK_NTCLOSE_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntclose
 */
/* clang-format off */
typedef NTSTATUS (*T_NtClose)(
    /* [IN] */  HANDLE  Handle
);
/* clang-format on */

/**
 * @brief NtClose() direct call.
 */
extern T_NtClose sys_NtClose;
}

namespace appbox
{

/**
 * @brief Hook NtClose().
 */
extern HookRecord HookNtClose;

} // namespace appbox

#endif
