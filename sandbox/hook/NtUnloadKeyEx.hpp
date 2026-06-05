#ifndef APPBOX_SANDBOX_HOOK_NTUNLOADKEYEX_HPP
#define APPBOX_SANDBOX_HOOK_NTUNLOADKEYEX_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntunloadkeyex
 */
/* clang-format off */
typedef NTSTATUS (*T_NtUnloadKeyEx)(
    /* [IN] */              POBJECT_ATTRIBUTES  TargetKey,
    /* [IN, OPTIONAL] */    HANDLE              Event
);
/* clang-format on */

/**
 * @brief NtUnloadKeyEx() direct call.
 */
extern T_NtUnloadKeyEx sys_NtUnloadKeyEx;
}

namespace appbox
{

/**
 * @brief Hook NtUnloadKeyEx().
 */
extern HookRecord HookNtUnloadKeyEx;

} // namespace appbox

#endif
