#ifndef APPBOX_SANDBOX_HOOK_NTSAVEKEYEX_HPP
#define APPBOX_SANDBOX_HOOK_NTSAVEKEYEX_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwsavekeyex
 */
/* clang-format off */
typedef NTSTATUS (*T_NtSaveKeyEx)(
    /* [IN] */ HANDLE KeyHandle,
    /* [IN] */ HANDLE FileHandle,
    /* [IN] */ ULONG  Format
);
/* clang-format on */

/**
 * @brief NtSaveKeyEx() direct call.
 */
extern T_NtSaveKeyEx sys_NtSaveKeyEx;
}

namespace appbox
{

/**
 * @brief Hook NtSaveKeyEx().
 */
extern HookRecord HookNtSaveKeyEx;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTSAVEKEYEX_HPP
