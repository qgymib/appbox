#ifndef APPBOX_SANDBOX_HOOK_NTLOADKEY2_HPP
#define APPBOX_SANDBOX_HOOK_NTLOADKEY2_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntloadkey2
 */
/* clang-format off */
typedef NTSTATUS (*T_NtLoadKey2)(
    /* [IN] */  POBJECT_ATTRIBUTES  TargetKey,
    /* [IN] */  POBJECT_ATTRIBUTES  SourceFile,
    /* [IN] */  ULONG               Flags
);
/* clang-format on */

/**
 * @brief NtLoadKey2() direct call
 */
extern T_NtLoadKey2 sys_NtLoadKey2;
}

namespace appbox
{

/**
 * @brief Hook NtLoadKey2().
 */
extern HookRecord HookNtLoadKey2;

} // namespace appbox

#endif
