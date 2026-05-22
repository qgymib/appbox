#ifndef APPBOX_SANDBOX_HOOK_NTSETINFORMATIONFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTSETINFORMATIONFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntsetinformationfile
 */
typedef NTSTATUS (*T_NtSetInformationFile)(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                           ULONG Length, FILE_INFORMATION_CLASS FileInformationClass);

/**
 * @brief NtSetInformationFile() direct call.
 */
extern T_NtSetInformationFile sys_NtSetInformationFile;
}

namespace appbox
{

/**
 * @brief Inject NtSetInformationFile() hook.
 */
void InjectNtSetInformationFile();

} // namespace appbox

#endif
