#ifndef APPBOX_SANDBOX_HOOK_NTOPENKEYTRANSACTEDEX_HPP
#define APPBOX_SANDBOX_HOOK_NTOPENKEYTRANSACTEDEX_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwopenkeytransactedex
 */
/* clang-format off */
typedef NTSTATUS (*T_NtOpenKeyTransactedEx)(
    /* [OUT] */ PHANDLE             KeyHandle,
    /* [IN] */  ACCESS_MASK         DesiredAccess,
    /* [IN] */  POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [IN] */  ULONG               OpenOptions,
    /* [IN] */  HANDLE              TransactionHandle
);
/* clang-format on */

/**
 * @brief NtOpenKeyTransactedEx() direct call.
 */
extern T_NtOpenKeyTransactedEx sys_NtOpenKeyTransactedEx;
}

namespace appbox
{

/**
 * @brief Hook NtOpenKeyTransactedEx().
 */
extern HookRecord HookNtOpenKeyTransactedEx;

} // namespace appbox

#endif
