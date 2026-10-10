#ifndef APPBOX_SANDBOX_HOOK_NTLOCKREGISTRYKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTLOCKREGISTRYKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntlockregistrykey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtLockRegistryKey)(
    /* [IN] */ HANDLE KeyHandle
);
/* clang-format on */

/**
 * @brief NtLockRegistryKey() direct call.
 */
extern T_NtLockRegistryKey sys_NtLockRegistryKey;
}

namespace appbox
{

/**
 * @brief Hook NtLockRegistryKey().
 *
 * The call locks the hive which holds the key, so a handle of the host layer
 * is refused and a handle of the hive is forwarded.
 */
extern HookRecord HookNtLockRegistryKey;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTLOCKREGISTRYKEY_HPP
