#ifndef APPBOX_SANDBOX_HOOK_NTRENAMEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTRENAMEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntrenamekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtRenameKey)(
    /* [IN] */  HANDLE          KeyHandle,
    /* [IN] */  PUNICODE_STRING NewName
);
/* clang-format on */

/**
 * @brief NtRenameKey() direct call.
 */
extern T_NtRenameKey sys_NtRenameKey;
}

namespace appbox
{

/**
 * @brief Hook NtRenameKey().
 */
extern HookRecord HookNtRenameKey;

} // namespace appbox

#endif
