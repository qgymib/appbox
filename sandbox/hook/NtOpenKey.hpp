#ifndef APPBOX_SANDBOX_HOOK_NTOPENKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTOPENKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwopenkey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtOpenKey)(
    /* [OUT] */ PHANDLE             KeyHandle,
    /* [IN] */  ACCESS_MASK         DesiredAccess,
    /* [IN] */  POBJECT_ATTRIBUTES  ObjectAttributes
);
/* clang-format on */

/**
 * @brief NtOpenKey() direct call.
 */
extern T_NtOpenKey sys_NtOpenKey;
}

namespace appbox
{

/**
 * @brief Hook NtOpenKey().
 */
extern HookRecord HookNtOpenKey;

} // namespace appbox

#endif
