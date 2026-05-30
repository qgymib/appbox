#ifndef APPBOX_SANDBOX_HOOK_NTWRITEFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTWRITEFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntwritefile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtWriteFile)(
    /* [IN] */          HANDLE              FileHandle,
    /* [IN,OPTIONAL] */ HANDLE              Event,
    /* [IN,OPTIONAL] */ PIO_APC_ROUTINE     ApcRoutine,
    /* [IN,OPTIONAL] */ PVOID               ApcContext,
    /* [OUT] */         PIO_STATUS_BLOCK    IoStatusBlock,
    /* [IN] */          PVOID               Buffer,
    /* [IN] */          ULONG               Length,
    /* [IN,OPTIONAL] */ PLARGE_INTEGER      ByteOffset,
    /* [IN,OPTIONAL] */ PULONG              Key
);
/* clang-format on */

/**
 * @brief NtWriteFile() direct call.
 */
extern T_NtWriteFile sys_NtWriteFile;
}

namespace appbox
{

/**
 * @brief Inject NtWriteFile() hook.
 */
void AttachNtWriteFile();
void DetachNtWriteFile();

} // namespace appbox

#endif
