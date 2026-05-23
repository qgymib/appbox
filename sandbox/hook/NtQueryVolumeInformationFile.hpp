#ifndef APPBOX_SANDBOX_HOOK_NTQUERYVOLUMEINFORMATIONFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYVOLUMEINFORMATIONFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryvolumeinformationfile
 */
typedef NTSTATUS (*T_NtQueryVolumeInformationFile)(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                                                   PVOID FsInformation, ULONG Length,
                                                   FS_INFORMATION_CLASS FsInformationClass);

/**
 * @brief NtQueryVolumeInformationFile() direct call.
 */
extern T_NtQueryVolumeInformationFile sys_NtQueryVolumeInformationFile;
}

namespace appbox
{

/**
 * @brief Inject NtQueryVolumeInformationFile() hook.
 */
void InjectNtQueryVolumeInformationFile();

} // namespace appbox

#endif
