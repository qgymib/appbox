#ifndef APPBOX_SANDBOX_HOOK_NTSETINFORMATIONKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTSETINFORMATIONKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntsetinformationkey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtSetInformationKey)(
    /* [IN] */ HANDLE                    KeyHandle,
    /* [IN] */ KEY_SET_INFORMATION_CLASS KeySetInformationClass,
    /* [IN] */ PVOID                     KeySetInformation,
    /* [IN] */ ULONG                     KeySetInformationLength
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
 *
 * The call changes a property of the key its handle denotes, so a handle of
 * the host layer is refused and a handle of the hive is forwarded.
 */
extern HookRecord HookNtSetInformationKey;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTSETINFORMATIONKEY_HPP
