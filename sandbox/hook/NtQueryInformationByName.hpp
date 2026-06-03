#ifndef APPBOX_SANDBOX_HOOK_NTQUERYINFORMATIONBYNAME_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYINFORMATIONBYNAME_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationbyname
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryInformationByName)(
    /* [IN] */  POBJECT_ATTRIBUTES      ObjectAttributes,
    /* [OUT] */ PIO_STATUS_BLOCK        IoStatusBlock,
    /* [OUT] */ PVOID                   FileInformation,
    /* [IN] */  ULONG                   Length,
    /* [IN] */  FILE_INFORMATION_CLASS  FileInformationClass
);
/* clang-format on */

/**
 * @brief NtQueryInformationByName() direct call.
 */
extern T_NtQueryInformationByName sys_NtQueryInformationByName;
}

namespace appbox
{

/**
 * @brief Hook NtQueryInformationByName().
 */
extern HookRecord HookNtQueryInformationByName;

} // namespace appbox

#endif
