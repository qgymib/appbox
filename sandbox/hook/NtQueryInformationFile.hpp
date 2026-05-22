#ifndef APPBOX_SANDBOX_HOOK_NTQUERYINFORMATIONFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYINFORMATIONFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationfile
 */
typedef NTSTATUS (*T_NtQueryInformationFile)(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                             ULONG Length, FILE_INFORMATION_CLASS FileInformationClass);

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
void InjectNtQueryInformationFile();

} // namespace appbox

#endif
