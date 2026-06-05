#ifndef APPBOX_SANDBOX_HOOK_NTSETINFORMATIONKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTSETINFORMATIONKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntsetinformationkey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtSetInformationKey)(
    /* [IN] */  HANDLE                      KeyHandle,
    /* [IN] */  KEY_SET_INFORMATION_CLASS   KeySetInformationClass,
    /* [IN] */  PVOID                       KeySetInformation,
    /* [IN] */  ULONG                       KeySetInformationLength
);
/* clang-format on */

/**
 * @brief NtSetInformationKey() direct call.
 */
extern T_NtSetInformationKey sys_NtSetInformationKey;
}

namespace appbox
{

/**
 * @brief Hook NtSetInformationKey().
 */
extern HookRecord HookNtSetInformationKey;

} // namespace appbox

#endif
