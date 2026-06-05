#ifndef APPBOX_SANDBOX_HOOK_NTLOADKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTLOADKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntloadkey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtLoadKey)(
    /* [IN] */  POBJECT_ATTRIBUTES  TargetKey,
    /* [IN] */  POBJECT_ATTRIBUTES  SourceFile
);
/* clang-format on */

/**
 * @brief NtLoadKey() direct call.
 */
extern T_NtLoadKey sys_NtLoadKey;
}

namespace appbox
{

/**
 * @brief Hook NtLoadKey().
 */
extern HookRecord HookNtLoadKey;

} // namespace appbox

#endif
