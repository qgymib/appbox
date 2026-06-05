#ifndef APPBOX_SANDBOX_HOOK_NTCREATEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTCREATEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtCreateKey)(
    /* [OUT] */             PHANDLE             KeyHandle,
    /* [IN] */              ACCESS_MASK         DesiredAccess,
    /* [IN] */              POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [IN] */              ULONG               TitleIndex,
    /* [IN,OPTIONAL] */     PUNICODE_STRING     Class,
    /* [IN] */              ULONG               CreateOptions,
    /* [OUT,OPTIONAL] */    PULONG              Disposition
);
/* clang-format on */

/**
 * @brief NtCreateKey() direct call.
 */
extern T_NtCreateKey sys_NtCreateKey;
}

namespace appbox
{

/**
 * @brief Hook NtCreateKey().
 */
extern HookRecord HookNtCreateKey;

} // namespace appbox

#endif
