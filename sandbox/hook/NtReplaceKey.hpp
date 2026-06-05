#ifndef APPBOX_SANDBOX_HOOK_NTREPLACEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTREPLACEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntreplacekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtReplaceKey)(
    /* [IN] */  POBJECT_ATTRIBUTES  NewFile,
    /* [IN] */  HANDLE              TargetHandle,
    /* [IN] */  POBJECT_ATTRIBUTES  OldFile
);
/* clang-format on */

/**
 * @brief NtReplaceKey() direct call.
 */
extern T_NtReplaceKey sys_NtReplaceKey;
}

namespace appbox
{

/**
 * @brief Hook NtReplaceKey().
 */
extern HookRecord HookNtReplaceKey;

} // namespace appbox

#endif
