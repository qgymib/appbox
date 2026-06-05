#ifndef APPBOX_SANDBOX_HOOK_NTDELETEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTDELETEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwdeletekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtDeleteKey)(
    /* [IN] */  HANDLE  KeyHandle
);
/* clang-format on */

/**
 * @brief NtDeleteKey() direct call.
 */
extern T_NtDeleteKey sys_NtDeleteKey;
}

namespace appbox
{

/**
 * @brief Hook NtDeleteKey().
 */
extern HookRecord HookNtDeleteKey;

} // namespace appbox

#endif
