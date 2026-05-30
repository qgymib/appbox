#ifndef APPBOX_SANDBOX_HOOK_NTFSCONTROLFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTFSCONTROLFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntfscontrolfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtFsControlFile)(
    /* [IN] */              HANDLE              FileHandle,
    /* [IN,OPTIONAL] */     HANDLE              Event,
    /* [IN,OPTIONAL] */     PIO_APC_ROUTINE     ApcRoutine,
    /* [IN,OPTIONAL] */     PVOID               ApcContext,
    /* [OUT] */             PIO_STATUS_BLOCK    IoStatusBlock,
    /* [IN] */              ULONG               FsControlCode,
    /* [IN,OPTIONAL] */     PVOID               InputBuffer,
    /* [IN] */              ULONG               InputBufferLength,
    /* [OUT,OPTIONAL] */    PVOID               OutputBuffer,
    /* [IN] */              ULONG               OutputBufferLength
);
/* clang-format on */

/**
 * @brief NtFsControlFile() direct call.
 */
extern T_NtFsControlFile sys_NtFsControlFile;
}

namespace appbox
{

/**
 * @brief Inject NtFsControlFile() hook.
 */
void AttachNtFsControlFile();
void DetachNtFsControlFile();

} // namespace appbox

#endif
