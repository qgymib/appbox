#ifndef APPBOX_SANDBOX_HOOK_NTLOADKEYEX_HPP
#define APPBOX_SANDBOX_HOOK_NTLOADKEYEX_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntloadkeyex
 */
/* clang-format off */
typedef NTSTATUS (*T_NtLoadKeyEx)(
    /* [IN] */              POBJECT_ATTRIBUTES  TargetKey,
    /* [IN] */              POBJECT_ATTRIBUTES  SourceFile,
    /* [IN] */              ULONG               Flags,
    /* [IN, OPTIONAL] */    HANDLE              TrustClassKey,
    /* [IN, OPTIONAL] */    HANDLE              Event,
    /* [IN, OPTIONAL] */    ACCESS_MASK         DesiredAccess,
    /* [OUT, OPTIONAL] */   PHANDLE             RootHandle,
    /* [IN, OPTIONAL] */    PVOID               Reserved
);
/* clang-format on */

/**
 * @brief NtLoadKeyEx() direct call.
 */
extern T_NtLoadKeyEx sys_NtLoadKeyEx;
}

namespace appbox
{

/**
 * @brief Hook NtLoadKeyEx().
 */
extern HookRecord HookNtLoadKeyEx;

} // namespace appbox

#endif
