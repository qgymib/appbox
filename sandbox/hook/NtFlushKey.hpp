#ifndef APPBOX_SANDBOX_HOOK_NTFLUSHKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTFLUSHKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntflushkey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtFlushKey)(
    /* [IN] */ HANDLE KeyHandle
);
/* clang-format on */

/**
 * @brief NtFlushKey() direct call.
 */
extern T_NtFlushKey sys_NtFlushKey;
}

namespace appbox
{

/**
 * @brief Hook NtFlushKey().
 *
 * The flush writes the hive which holds the key to disk, so a handle of the
 * host layer is refused and a handle of the hive is forwarded.
 */
extern HookRecord HookNtFlushKey;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTFLUSHKEY_HPP
