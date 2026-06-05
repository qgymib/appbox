#ifndef APPBOX_SANDBOX_HOOK_NTSAVEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTSAVEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntsavekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtSaveKey)(
    /* [IN] */  HANDLE  KeyHandle,
    /* [IN] */  HANDLE  FileHandle
);
/* clang-format on */

/**
 * @brief NtSaveKey() direct call.
 */
extern T_NtSaveKey sys_NtSaveKey;
}

namespace appbox
{

/**
 * @brief Hook NtSaveKey().
 */
extern HookRecord HookNtSaveKey;

} // namespace appbox

#endif
