#ifndef APPBOX_SANDBOX_HOOK_NTCREATEKEYTRANSACTED_HPP
#define APPBOX_SANDBOX_HOOK_NTCREATEKEYTRANSACTED_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntcreatekeytransacted
 */
/* clang-format off */
typedef NTSTATUS (*T_NtCreateKeyTransacted)(
    /* [OUT] */             PHANDLE             KeyHandle,
    /* [IN] */              ACCESS_MASK         DesiredAccess,
    /* [IN] */              POBJECT_ATTRIBUTES  ObjectAttributes,
    /* [IN] */              ULONG               TitleIndex,
    /* [IN,OPTIONAL] */     PCUNICODE_STRING    Class,
    /* [IN] */              ULONG               CreateOptions,
    /* [IN] */              HANDLE              TransactionHandle,
    /* [OUT,OPTIONAL] */    PULONG              Disposition
);
/* clang-format on */

/**
 * @brief NtCreateKeyTransacted() direct call.
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

#endif
