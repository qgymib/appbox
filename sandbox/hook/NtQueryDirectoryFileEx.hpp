#ifndef APPBOX_SANDBOX_HOOK_NTQUERYDIRECTORYFILEEX_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYDIRECTORYFILEEX_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfileex
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryDirectoryFileEx)(
    /* [IN] */          HANDLE                  FileHandle,
    /* [IN,OPTIONAL]*/  HANDLE                  Event,
    /* [IN,OPTIONAL]*/  PIO_APC_ROUTINE         ApcRoutine,
    /* [IN,OPTIONAL]*/  PVOID                   ApcContext,
    /* [OUT] */         PIO_STATUS_BLOCK        IoStatusBlock,
    /* [OUT] */         PVOID                   FileInformation,
    /* [IN] */          ULONG                   Length,
    /* [IN] */          FILE_INFORMATION_CLASS  FileInformationClass,
    /* [IN] */          ULONG                   QueryFlags,
    /* [IN,OPTIONAL]*/  PUNICODE_STRING         FileName
);
/* clang-format on */

/**
 * @brief NtQueryDirectoryFileEx() direct call.
 */
extern T_NtQueryDirectoryFileEx sys_NtQueryDirectoryFileEx;
}

namespace appbox
{

/**
 * @brief Hook NtQueryDirectoryFileEx().
 */
extern HookRecord HookNtQueryDirectoryFileEx;

} // namespace appbox

#endif
