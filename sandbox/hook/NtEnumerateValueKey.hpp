#ifndef APPBOX_SANDBOX_HOOK_NTENUMERATEVALUEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTENUMERATEVALUEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwenumeratevaluekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtEnumerateValueKey)(
    /* [IN] */              HANDLE                      KeyHandle,
    /* [IN] */              ULONG                       Index,
    /* [IN] */              KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    /* [OUT, OPTIONAL] */   PVOID                       KeyValueInformation,
    /* [IN] */              ULONG                       Length,
    /* [OUT] */             PULONG                      ResultLength
);
/* clang-format on */

/**
 * @brief NtEnumerateValueKey() direct call.
 */
extern T_NtEnumerateValueKey sys_NtEnumerateValueKey;
}

namespace appbox
{

/**
 * @brief Hook NtEnumerateValueKey().
 */
extern HookRecord HookNtEnumerateValueKey;

} // namespace appbox

#endif
