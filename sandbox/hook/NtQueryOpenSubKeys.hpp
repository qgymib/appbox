#ifndef APPBOX_SANDBOX_HOOK_NTQUERYOPENSUBKEYS_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYOPENSUBKEYS_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntqueryopensubkeys
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryOpenSubKeys)(
    /* [IN] */  POBJECT_ATTRIBUTES  TargetKey,
    /* [OUT] */ PULONG              HandleCount
);
/* clang-format on */

/**
 * @brief NtQueryOpenSubKeys() direct call.
 */
extern T_NtQueryOpenSubKeys sys_NtQueryOpenSubKeys;
}

namespace appbox
{

/**
 * @brief Hook NtQueryOpenSubKeys().
 */
extern HookRecord HookNtQueryOpenSubKeys;

} // namespace appbox

#endif
