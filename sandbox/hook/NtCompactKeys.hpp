#ifndef APPBOX_SANDBOX_HOOK_NTCOMPACTKEYS_HPP
#define APPBOX_SANDBOX_HOOK_NTCOMPACTKEYS_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntcompactkeys
 */
/* clang-format off */
typedef NTSTATUS (*T_NtCompactKeys)(
    /* [IN] */  ULONG   Count,
    /* [IN] */  HANDLE  KeyArray[]
);
/* clang-format on */

/**
 * @brief NtCompactKeys() direct call.
 */
extern T_NtCompactKeys sys_NtCompactKeys;
}

namespace appbox
{

/**
 * @brief Hook NtCompactKeys().
 */
extern HookRecord HookNtCompactKeys;

} // namespace appbox

#endif
