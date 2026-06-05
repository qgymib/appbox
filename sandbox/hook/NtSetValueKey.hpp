#ifndef APPBOX_SANDBOX_HOOK_NTSETVALUEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTSETVALUEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwenumeratevaluekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtSetValueKey)(
    /* [IN] */              HANDLE                      KeyHandle,
    /* [IN] */              ULONG                       Index,
    /* [IN] */              KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    /* [OUT, OPTIONAL] */   PVOID                       KeyValueInformation,
    /* [IN] */              ULONG                       Length,
    /* [OUT] */             PULONG                      ResultLength
);
/* clang-format on */

/**
 * @brief NtSetValueKey() direct call.
 */
extern T_NtSetValueKey sys_NtSetValueKey;
}

namespace appbox
{

/**
 * @brief Hook NtSetValueKey().
 */
extern HookRecord HookNtSetValueKey;

} // namespace appbox

#endif
