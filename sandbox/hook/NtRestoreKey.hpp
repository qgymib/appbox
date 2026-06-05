#ifndef APPBOX_SANDBOX_HOOK_NTRESTOREKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTRESTOREKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntrestorekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtRestoreKey)(
    /* [IN] */  HANDLE  KeyHandle,
    /* [IN] */  HANDLE  FileHandle,
    /* [IN] */  ULONG   Flags
);
/* clang-format on */

/**
 * @brief NtRestoreKey() direct call.
 */
extern T_NtRestoreKey sys_NtRestoreKey;
}

namespace appbox
{

/**
 * @brief Hook NtRestoreKey().
 */
extern HookRecord HookNtRestoreKey;

} // namespace appbox

#endif
