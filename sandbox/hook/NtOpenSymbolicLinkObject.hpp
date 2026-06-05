#ifndef APPBOX_SANDBOX_HOOK_NTOPENSYMBOLICLINKOBJECT_HPP
#define APPBOX_SANDBOX_HOOK_NTOPENSYMBOLICLINKOBJECT_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/devnotes/ntopensymboliclinkobject
 */
/* clang-format off */
typedef NTSTATUS (*T_NtOpenSymbolicLinkObject)(
    /* [OUT] */ PHANDLE             LinkHandle,
    /* [IN] */  ACCESS_MASK         DesiredAccess,
    /* [IN] */  POBJECT_ATTRIBUTES  ObjectAttributes
);
/* clang-format on */

/**
 * @brief NtOpenSymbolicLinkObject() direct call.
 */
extern T_NtOpenSymbolicLinkObject sys_NtOpenSymbolicLinkObject;
}

namespace appbox
{

/**
 * @brief Hook NtOpenSymbolicLinkObject().
 */
extern HookRecord HookNtOpenSymbolicLinkObject;

} // namespace appbox

#endif
