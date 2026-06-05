#ifndef APPBOX_SANDBOX_HOOK_NTQUERYVALUEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYVALUEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwqueryvaluekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryValueKey)(
    /* [IN] */              HANDLE                      KeyHandle,
    /* [IN] */              PUNICODE_STRING             ValueName,
    /* [IN] */              KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    /* [OUT, OPTIONAL] */   PVOID                       KeyValueInformation,
    /* [IN] */              ULONG                       Length,
    /* [OUT] */             PULONG                      ResultLength
);
/* clang-format on */

/**
 * @brief NtQueryValueKey() direct call.
 */
extern T_NtQueryValueKey sys_NtQueryValueKey;
}

namespace appbox
{

/**
 * @brief Hook NtQueryValueKey().
 */
extern HookRecord HookNtQueryValueKey;

} // namespace appbox

#endif
