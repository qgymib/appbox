#ifndef APPBOX_SANDBOX_HOOK_NTQUERYOBJECT_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYOBJECT_HPP

#include "utils/WinAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject
 */
typedef NTSTATUS (*T_NtQueryObject)(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                    PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength);

/**
 * @brief NtQueryObject() direct call.
 */
extern T_NtQueryObject sys_NtQueryObject;

#ifdef __cplusplus
}
#endif

namespace appbox
{

/**
 * @brief Inject NtQueryObject() hook.
 */
void InjectNtQueryObject();

} // namespace appbox

#endif
