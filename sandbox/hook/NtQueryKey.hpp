#ifndef APPBOX_SANDBOX_HOOK_NTQUERYKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwquerykey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryKey)(
    /* [IN] */              HANDLE                  KeyHandle,
    /* [IN] */              KEY_INFORMATION_CLASS   KeyInformationClass,
    /* [OUT, OPTIONAL] */   PVOID                   KeyInformation,
    /* [IN] */              ULONG                   Length,
    /* [OUT] */             PULONG                  ResultLength
);
/* clang-format on */

/**
 * @brief NtQueryKey() direct call.
 */
extern T_NtQueryKey sys_NtQueryKey;
}

namespace appbox
{

/**
 * @brief Hook NtQueryKey().
 */
extern HookRecord HookNtQueryKey;

} // namespace appbox

#endif
