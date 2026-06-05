#ifndef APPBOX_SANDBOX_HOOK_NTNOTIFYCHANGEMULTIPLEKEYS_HPP
#define APPBOX_SANDBOX_HOOK_NTNOTIFYCHANGEMULTIPLEKEYS_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntnotifychangemultiplekeys
 */
/* clang-format off */
typedef NTSTATUS (*T_NtNotifyChangeMultipleKeys)(
    /* [IN] */              HANDLE              MasterKeyHandle,
    /* [IN, OPTIONAL] */    ULONG               Count,
    /* [IN] */              OBJECT_ATTRIBUTES   SubordinateObjects[],
    /* [IN, OPTIONAL] */    HANDLE              Event,
    /* [IN, OPTIONAL] */    PIO_APC_ROUTINE     ApcRoutine,
    /* [IN, OPTIONAL] */    PVOID               ApcContext,
    /* [OUT] */             PIO_STATUS_BLOCK    IoStatusBlock,
    /* [IN] */              ULONG               CompletionFilter,
    /* [IN] */              BOOLEAN             WatchTree,
    /* [OUT] */             PVOID               Buffer,
    /* [IN] */              ULONG               BufferSize,
    /* [IN] */              BOOLEAN             Asynchronous
);
/* clang-format on */

/**
 * @brief NtNotifyChangeMultipleKeys() direct call.
 */
extern T_NtNotifyChangeMultipleKeys sys_NtNotifyChangeMultipleKeys;
}

namespace appbox
{

/**
 * @brief Hook NtNotifyChangeMultipleKeys().
 */
extern HookRecord HookNtNotifyChangeMultipleKeys;

} // namespace appbox

#endif
