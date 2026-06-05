#ifndef APPBOX_SANDBOX_HOOK_NTOPENKEYTRANSACTED_HPP
#define APPBOX_SANDBOX_HOOK_NTOPENKEYTRANSACTED_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwopenkeytransacted
 */
/* clang-format off */
typedef NTSTATUS (*T_NtOpenKeyTransacted)(
    /* [OUT] */ PHANDLE             KeyHandle,
    /* [IN] */  ACCESS_MASK         DesiredAccess,
    /* [IN] */  POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [IN] */  HANDLE              TransactionHandle
);
/* clang-format on */

/**
 * @brief NtOpenKeyTransacted() direct call.
 */
extern T_NtOpenKeyTransacted sys_NtOpenKeyTransacted;
}

namespace appbox
{

/**
 * @brief Hook NtOpenKeyTransacted().
 */
extern HookRecord HookNtOpenKeyTransacted;

} // namespace appbox

#endif
