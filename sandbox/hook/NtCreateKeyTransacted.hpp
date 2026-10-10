#ifndef APPBOX_SANDBOX_HOOK_NTCREATEKEYTRANSACTED_HPP
#define APPBOX_SANDBOX_HOOK_NTCREATEKEYTRANSACTED_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatekeytransacted
 */
/* clang-format off */
typedef NTSTATUS (*T_NtCreateKeyTransacted)(
    /* [OUT] */             PHANDLE             KeyHandle,
    /* [IN] */              ACCESS_MASK         DesiredAccess,
    /* [IN] */              POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [IN] */              ULONG               TitleIndex,
    /* [IN,OPTIONAL] */     PUNICODE_STRING     Class,
    /* [IN] */              ULONG               CreateOptions,
    /* [IN] */              HANDLE              TransactionHandle,
    /* [OUT,OPTIONAL] */    PULONG              Disposition
);
/* clang-format on */

/**
 * @brief NtCreateKeyTransacted() direct call.
 *
 * The transaction handle precedes the disposition, which is the order of the
 * entry point and not the one of NtCreateKey.
 */
extern T_NtCreateKeyTransacted sys_NtCreateKeyTransacted;
}

namespace appbox
{

/**
 * @brief Hook NtCreateKeyTransacted().
 */
extern HookRecord HookNtCreateKeyTransacted;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTCREATEKEYTRANSACTED_HPP
