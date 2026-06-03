#ifndef APPBOX_SANDBOX_HOOK_NTSETINFORMATIONFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTSETINFORMATIONFILE_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntsetinformationfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtSetInformationFile)(
    /* [IN] */  HANDLE                  FileHandle,
    /* [OUT] */ PIO_STATUS_BLOCK        IoStatusBlock,
    /* [IN] */  PVOID                   FileInformation,
    /* [IN] */  ULONG                   Length,
    /* [IN] */  FILE_INFORMATION_CLASS  FileInformationClass
);
/* clang-format on */

/**
 * @brief NtSetInformationFile() direct call.
 */
extern T_NtSetInformationFile sys_NtSetInformationFile;
}

namespace appbox
{

/**
 * @brief Hook NtSetInformationFile().
 */
extern HookRecord HookNtSetInformationFile;

} // namespace appbox

#endif
