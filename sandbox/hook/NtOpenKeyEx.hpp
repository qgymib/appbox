#ifndef APPBOX_SANDBOX_HOOK_NTOPENKEYEX_HPP
#define APPBOX_SANDBOX_HOOK_NTOPENKEYEX_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntopenkeyex
 */
/* clang-format off */
typedef NTSTATUS (*T_NtOpenKeyEx)(
    /* [OUT] */ PHANDLE             KeyHandle,
    /* [IN] */  ACCESS_MASK         DesiredAccess,
    /* [IN] */  POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [IN] */  ULONG               OpenOptions
);
/* clang-format on */

/**
 * @brief NtOpenKeyEx() direct call.
 */
extern T_NtOpenKeyEx sys_NtOpenKeyEx;
}

namespace appbox
{

/**
 * @brief Hook NtOpenKeyEx().
 */
extern HookRecord HookNtOpenKeyEx;

} // namespace appbox

#endif
