#ifndef APPBOX_SANDBOX_HOOK_NTLOADKEY3_HPP
#define APPBOX_SANDBOX_HOOK_NTLOADKEY3_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntloadkey3
 */
/* clang-format off */
typedef NTSTATUS (*T_NtLoadKey3)(
    /* [IN] */              POBJECT_ATTRIBUTES      TargetKey,
    /* [IN] */              POBJECT_ATTRIBUTES      SourceFile,
    /* [IN] */              ULONG                   Flags,
    /* [IN] */              PVOID                   ExtendedParameters, // was type PCM_EXTENDED_PARAMETER
    /* [IN] */              ULONG                   ExtendedParameterCount,
    /* [IN, OPTIONAL] */    ACCESS_MASK             DesiredAccess,
    /* [OUT, OPTIONAL] */   PHANDLE                 RootHandle,
    /* [IN] */              PVOID                   Reserved
);
/* clang-format on */

/**
 * @brief NtLoadKey3() direct call.
 */
extern T_NtLoadKey3 sys_NtLoadKey3;
}

namespace appbox
{

/**
 * @brief Hook NtLoadKey3().
 */
extern HookRecord HookNtLoadKey3;

} // namespace appbox

#endif
