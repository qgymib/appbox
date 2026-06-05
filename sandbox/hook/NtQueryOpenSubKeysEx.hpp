#ifndef APPBOX_SANDBOX_HOOK_NTQUERYOPENSUBKEYSEX_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYOPENSUBKEYSEX_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntqueryopensubkeysex
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryOpenSubKeysEx)(
    /* [IN] */  POBJECT_ATTRIBUTES  TargetKey,
    /* [IN] */  ULONG               BufferLength,
    /* [OUT] */ PVOID               Buffer,
    /* [OUT] */ PULONG              RequiredSize
);
/* clang-format on */

/**
 * @brief NtQueryOpenSubKeysEx() direct call.
 */
extern T_NtQueryOpenSubKeysEx sys_NtQueryOpenSubKeysEx;
}

namespace appbox
{

/**
 * @brief Hook NtQueryOpenSubKeysEx().
 */
extern HookRecord HookNtQueryOpenSubKeysEx;

} // namespace appbox

#endif
