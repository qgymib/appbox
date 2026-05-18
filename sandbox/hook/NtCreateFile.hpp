#ifndef APPBOX_SANDBOX_HOOK_NTCREATEFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTCREATEFILE_HPP

#include "utils/WinAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile
 */
typedef NTSTATUS (*T_NtCreateFile)(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                   PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes,
                                   ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer,
                                   ULONG EaLength);

/**
 * @brief NtCreateFile() direct call.
 */
extern T_NtCreateFile sys_NtCreateFile;

#ifdef __cplusplus
}
#endif

namespace appbox
{

/**
 * @brief Inject NtCreateFile() hook.
 */
void InjectNtCreateFile();

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTCREATEFILE_HPP
