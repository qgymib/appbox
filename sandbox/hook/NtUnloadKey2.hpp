#ifndef APPBOX_SANDBOX_HOOK_NTUNLOADKEY2_HPP
#define APPBOX_SANDBOX_HOOK_NTUNLOADKEY2_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntunloadkey2
 */
/* clang-format off */
typedef NTSTATUS (*T_NtUnloadKey2)(
    /* [IN] */  POBJECT_ATTRIBUTES  TargetKey,
    /* [IN] */  ULONG               Flags
);
/* clang-format on */

/**
 * @brief NtUnloadKey2() direct call.
 */
extern T_NtUnloadKey2 sys_NtUnloadKey2;
}

namespace appbox
{

/**
 * @brief Hook NtUnloadKey2().
 */
extern HookRecord HookNtUnloadKey2;

} // namespace appbox

#endif
