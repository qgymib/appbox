#ifndef APPBOX_SANDBOX_HOOK_NTOPENFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTOPENFILE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntopenfile
 */
typedef NTSTATUS (*T_NtOpenFile)(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions);

/**
 * @brief NtOpenFile() direct call.
 */
extern T_NtOpenFile sys_NtOpenFile;
}

namespace appbox
{

/**
 * @brief Inject NtOpenFile() hook.
 */
void InjectNtOpenFile();

} // namespace appbox

#endif
