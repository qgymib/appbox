#ifndef APPBOX_SANDBOX_HOOK_NTQUERYOBJECT_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYOBJECT_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryObject)(
    /* [IN,OPTIONAL] */     HANDLE                      Handle,
    /* [IN] */              OBJECT_INFORMATION_CLASS    ObjectInformationClass,
    /* [OUT,OPTIONAL] */    PVOID                       ObjectInformation,
    /* [IN] */              ULONG                       ObjectInformationLength,
    /* [OUT,OPTIONAL] */    PULONG                      ReturnLength
);
/* clang-format on */

/**
 * @brief NtQueryObject() direct call.
 */
extern T_NtQueryObject sys_NtQueryObject;
}

namespace appbox
{

/**
 * @brief Inject NtQueryObject() hook.
 */
void AttachNtQueryObject();
void DetachNtQueryObject();

} // namespace appbox

#endif
