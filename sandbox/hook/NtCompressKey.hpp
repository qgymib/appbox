#ifndef APPBOX_SANDBOX_HOOK_NTCOMPRESSKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTCOMPRESSKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @see https://ntdoc.m417z.com/ntcompresskey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtCompressKey)(
    /* [IN] */  HANDLE  KeyHandle
);
/* clang-format on */

/**
 * @brief NtCompressKey() direct call.
 */
extern T_NtCompressKey sys_NtCompressKey;
}

namespace appbox
{

/**
 * @brief Hook NtCompressKey().
 */
extern HookRecord HookNtCompressKey;

} // namespace appbox

#endif
