#ifndef APPBOX_SANDBOX_HOOK_NTOPENFILE_HPP
#define APPBOX_SANDBOX_HOOK_NTOPENFILE_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntopenfile
 */
/* clang-format off */
typedef NTSTATUS (*T_NtOpenFile)(
    /* [OUT] */ PHANDLE             FileHandle,
    /* [IN] */  ACCESS_MASK         DesiredAccess,
    /* [IN] */  POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [OUT] */ PIO_STATUS_BLOCK    IoStatusBlock,
    /* [IN] */  ULONG               ShareAccess,
    /* [IN] */  ULONG               OpenOptions
);
/* clang-format on */

/**
 * @brief NtOpenFile() direct call.
 */
extern T_NtOpenFile sys_NtOpenFile;
}

namespace appbox
{

/**
 * @brief Hook NtOpenFile().
 */
extern HookRecord HookNtOpenFile;

} // namespace appbox

#endif
