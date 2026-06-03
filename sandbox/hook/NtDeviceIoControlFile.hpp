#ifndef APPBOX_SANDBOX_HOOK_NTDEVICEIOCONTROLFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTDEVICEIOCONTROLFILE_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntdeviceiocontrolfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtDeviceIoControlFile)(
    /* [IN] */  HANDLE              FileHandle,
    /* [IN] */  HANDLE              Event,
    /* [IN] */  PIO_APC_ROUTINE     ApcRoutine,
    /* [IN] */  PVOID               ApcContext,
    /* [OUT] */ PIO_STATUS_BLOCK    IoStatusBlock,
    /* [IN] */  ULONG               IoControlCode,
    /* [IN] */  PVOID               InputBuffer,
    /* [IN] */  ULONG               InputBufferLength,
    /* [OUT] */ PVOID               OutputBuffer,
    /* [IN] */  ULONG               OutputBufferLength
);
/* clang-format on */

/**
 * @brief NtDeviceIoControlFile() direct call.
 */
extern T_NtDeviceIoControlFile sys_NtDeviceIoControlFile;
}

namespace appbox
{

/**
 * @brief Hook NtDeviceIoControlFile().
 */
extern HookRecord HookNtDeviceIoControlFile;

} // namespace appbox

#endif
