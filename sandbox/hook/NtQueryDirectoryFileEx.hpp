#ifndef APPBOX_SANDBOX_HOOK_NTQUERYDIRECTORYFILEEX_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYDIRECTORYFILEEX_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfileex
 */
typedef NTSTATUS (*T_NtQueryDirectoryFileEx)(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                             PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                             ULONG Length, FILE_INFORMATION_CLASS FileInformationClass,
                                             ULONG QueryFlags, PUNICODE_STRING FileName);

/**
 * @brief NtQueryDirectoryFileEx() direct call.
 */
extern T_NtQueryDirectoryFileEx sys_NtQueryDirectoryFileEx;
}

namespace appbox
{

/**
 * @brief Inject NtQueryDirectoryFileEx() hook.
 */
void InjectNtQueryDirectoryFileEx();

} // namespace appbox

#endif
