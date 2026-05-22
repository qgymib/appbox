#ifndef APPBOX_SANDBOX_HOOK_NTQUERYDIRECTORYFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYDIRECTORYFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile
 */
typedef NTSTATUS (*T_NtQueryDirectoryFile)(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                           PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                           ULONG Length, FILE_INFORMATION_CLASS FileInformationClass,
                                           BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan);

/**
 * @brief NtQueryDirectoryFile() direct call.
 */
extern T_NtQueryDirectoryFile sys_NtQueryDirectoryFile;
}

namespace appbox
{

/**
 * @brief Inject NtQueryDirectoryFile() hook.
 */
void InjectNtQueryDirectoryFile();

} // namespace appbox

#endif
