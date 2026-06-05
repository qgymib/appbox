#ifndef APPBOX_SANDBOX_HOOK_NTQUERYMULTIPLEVALUEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYMULTIPLEVALUEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerymultiplevaluekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryMultipleValueKey)(
    /* [IN] */              HANDLE              KeyHandle,
    /* [IN, OUT] */         PKEY_VALUE_ENTRY    ValueEntries,
    /* [IN] */              ULONG               EntryCount,
    /* [OUT] */             PVOID               ValueBuffer,
    /* [IN, OUT] */         PULONG              BufferLength,
    /* [OUT, OPTIONAL] */   PULONG              RequiredBufferLength
);
/* clang-format on */

/**
 * @brief NtQueryMultipleValueKey() direct call.
 */
extern T_NtQueryMultipleValueKey sys_NtQueryMultipleValueKey;
}

namespace appbox
{

/**
 * @brief Hook NtQueryMultipleValueKey().
 */
extern HookRecord HookNtQueryMultipleValueKey;

} // namespace appbox

#endif
