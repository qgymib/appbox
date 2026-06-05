#ifndef APPBOX_SANDBOX_HOOK_NTENUMERATEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTENUMERATEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/fr-fr/windows-hardware/drivers/ddi/wdm/nf-wdm-zwenumeratekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtEnumerateKey)(
    /* [IN] */              HANDLE                  KeyHandle,
    /* [IN] */              ULONG                   Index,
    /* [IN] */              KEY_INFORMATION_CLASS   KeyInformationClass,
    /* [OUT, OPTIONAL] */   PVOID                   KeyInformation,
    /* [IN] */              ULONG                   Length,
    /* [OUT] */             PULONG                  ResultLength
);
/* clang-format on */

/**
 * @brief NtEnumerateKey() direct call.
 */
extern T_NtEnumerateKey sys_NtEnumerateKey;
}

namespace appbox
{

/**
 * @brief Hook NtEnumerateKey().
 */
extern HookRecord HookNtEnumerateKey;

} // namespace appbox

#endif
