#ifndef APPBOX_SANDBOX_HOOK_NTDELETEVALUEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTDELETEVALUEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwdeletevaluekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtDeleteValueKey)(
    /* [IN] */  HANDLE          KeyHandle,
    /* [IN] */  PUNICODE_STRING ValueName
);
/* clang-format on */

/**
 * @brief NtDeleteValueKey() direct call.
 */
extern T_NtDeleteValueKey sys_NtDeleteValueKey;
}

namespace appbox
{

/**
 * @brief Hook NtDeleteValueKey().
 */
extern HookRecord HookNtDeleteValueKey;

} // namespace appbox

#endif
