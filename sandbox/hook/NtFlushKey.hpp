#ifndef APPBOX_SANDBOX_HOOK_NTFLUSHKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTFLUSHKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwflushkey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtFlushKey)(
    /* [IN] */  HANDLE  KeyHandle
);
/* clang-format on */

/**
 * @brief NtFlushKey() direct call.
 */
extern T_NtFlushKey sys_NtFlushKey;
}

namespace appbox
{

/**
 * @brief Hook NtFlushKey().
 */
extern HookRecord HookNtFlushKey;

} // namespace appbox

#endif
