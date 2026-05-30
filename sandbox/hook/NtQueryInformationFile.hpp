#ifndef APPBOX_SANDBOX_HOOK_NTQUERYINFORMATIONFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYINFORMATIONFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryInformationFile)(
    /* [IN] */  HANDLE                  FileHandle,
    /* [OUT] */ PIO_STATUS_BLOCK        IoStatusBlock,
    /* [OUT] */ PVOID                   FileInformation,
    /* [IN] */  ULONG                   Length,
    /* [IN] */  FILE_INFORMATION_CLASS  FileInformationClass
);
/* clang-format on */

/**
 * @brief NtQueryInformationFile() direct call.
 */
extern T_NtQueryInformationFile sys_NtQueryInformationFile;
}

namespace appbox
{

/**
 * @brief Inject NtQueryInformationFile() hook
 */
void AttachNtQueryInformationFile();
void DetachNtQueryInformationFile();

} // namespace appbox

#endif
