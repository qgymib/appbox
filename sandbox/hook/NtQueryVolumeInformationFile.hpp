#ifndef APPBOX_SANDBOX_HOOK_NTQUERYVOLUMEINFORMATIONFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYVOLUMEINFORMATIONFILE_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryvolumeinformationfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryVolumeInformationFile)(
    /* [IN] */  HANDLE                  FileHandle,
    /* [OUT] */ PIO_STATUS_BLOCK        IoStatusBlock,
    /* [OUT] */ PVOID                   FsInformation,
    /* [IN] */  ULONG                   Length,
    /* [IN] */  FS_INFORMATION_CLASS    FsInformationClass
);
/* clang-format on */

/**
 * @brief NtQueryVolumeInformationFile() direct call.
 */
extern T_NtQueryVolumeInformationFile sys_NtQueryVolumeInformationFile;
}

namespace appbox
{

/**
 * @brief Hook NtQueryVolumeInformationFile().
 */
extern HookRecord HookNtQueryVolumeInformationFile;

} // namespace appbox

#endif
