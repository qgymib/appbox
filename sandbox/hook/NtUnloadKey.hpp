#ifndef APPBOX_SANDBOX_HOOK_NTUNLOADKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTUNLOADKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntunloadkey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtUnloadKey)(
    /* [IN] */  POBJECT_ATTRIBUTES  TargetKey
);
/* clang-format on */

/**
 * @brief NtUnloadKey() direct call.
 */
extern T_NtUnloadKey sys_NtUnloadKey;
}

namespace appbox
{

/**
 * @brief Hook NtUnloadKey().
 */
extern HookRecord HookNtUnloadKey;

} // namespace appbox

#endif
