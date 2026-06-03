#ifndef APPBOX_SANDBOX_HOOK_NTQUERYDIRECTORYFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYDIRECTORYFILE_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryDirectoryFile)(
    /* [IN] */              HANDLE                  FileHandle,
    /* [IN, OPTIONAL] */    HANDLE                  Event,
    /* [IN, OPTIONAL] */    PIO_APC_ROUTINE         ApcRoutine,
    /* [IN, OPTIONAL] */    PVOID                   ApcContext,
    /* [OUT] */             PIO_STATUS_BLOCK        IoStatusBlock,
    /* [OUT] */             PVOID                   FileInformation,
    /* [IN] */              ULONG                   Length,
    /* [IN] */              FILE_INFORMATION_CLASS  FileInformationClass,
    /* [IN] */              BOOLEAN                 ReturnSingleEntry,
    /* [IN, OPTIONAL] */    PUNICODE_STRING         FileName,
    /* [IN] */              BOOLEAN                 RestartScan
);
/* clang-format on */

/**
 * @brief NtQueryDirectoryFile() direct call.
 */
extern T_NtQueryDirectoryFile sys_NtQueryDirectoryFile;
}

namespace appbox
{

/**
 * @brief Hook NtQueryDirectoryFile().
 */
extern HookRecord HookNtQueryDirectoryFile;

} // namespace appbox

#endif
