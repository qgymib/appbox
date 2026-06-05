#ifndef APPBOX_SANDBOX_HOOK_NTNOTIFYCHANGEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTNOTIFYCHANGEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwnotifychangekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtNotifyChangeKey)(
    /* [IN] */              HANDLE              KeyHandle,
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
 * @brief NtNotifyChangeKey() direct call.
 */
extern T_NtNotifyChangeKey sys_NtNotifyChangeKey;
}

namespace appbox
{

/**
 * @brief Hook NtNotifyChangeKey().
 */
extern HookRecord HookNtNotifyChangeKey;

} // namespace appbox

#endif
