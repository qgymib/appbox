#ifndef APPBOX_SANDBOX_HOOK_NTREADFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTREADFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/devnotes/ntreadfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtReadFile)(
    /* [IN] */          HANDLE              FileHandle,
    /* [IN,OPTIONAL] */ HANDLE              Event,
    /* [IN,OPTIONAL] */ PIO_APC_ROUTINE     ApcRoutine,
    /* [IN,OPTIONAL] */ PVOID               ApcContext,
    /* [OUT] */         PIO_STATUS_BLOCK    IoStatusBlock,
    /* [OUT] */         PVOID               Buffer,
    /* [IN] */          ULONG               Length,
    /* [IN,OPTIONAL] */ PLARGE_INTEGER      ByteOffset,
    /* [IN,OPTIONAL] */ PULONG              Key
);
/* clang-format on */

/**
 * @brief NtReadFile() direct call.
 */
extern T_NtReadFile sys_NtReadFile;
}

namespace appbox
{

/**
 * @brief Inject NtReadFile() hook.
 */
void AttachNtReadFile();
void DetachNtReadFile();

} // namespace appbox

#endif
