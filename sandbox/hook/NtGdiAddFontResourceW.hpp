#ifndef APPBOX_SANDBOX_HOOK_NTGDIADDFONTRESOURCEW_HPP
#define APPBOX_SANDBOX_HOOK_NTGDIADDFONTRESOURCEW_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @brief Entry point of the window manager which adds font resources.
 *
 * The prototype is not documented; it was measured on this build and is
 * described in docs/FontsIsolation.md: the buffer carries the paths of `cFiles`
 * files in NT form, `cwc` counts the characters of the buffer including the
 * terminating NUL of the last path, and the flags of the caller are passed
 * through unchanged (`AddFontResourceExW` adds `FR_PRIVATE` and an internal bit
 * of its own).
 *
 * The build refuses a buffer which carries several files, so the hook only
 * rewrites a call which names one file and forwards every other call.
 */
/* clang-format off */
typedef int (WINAPI *T_NtGdiAddFontResourceW)(
    /* [IN] */ LPCWSTR   pwszFiles,
    /* [IN] */ ULONG     cwc,
    /* [IN] */ ULONG     cFiles,
    /* [IN] */ ULONG     flags,
    /* [IN] */ ULONG_PTR reserved,
    /* [IN] */ PVOID     pdv
);
/* clang-format on */

/**
 * @brief NtGdiAddFontResourceW() direct call.
 */
extern T_NtGdiAddFontResourceW sys_NtGdiAddFontResourceW;
}

namespace appbox
{

/**
 * @brief Hook NtGdiAddFontResourceW().
 */
extern HookRecord HookNtGdiAddFontResourceW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTGDIADDFONTRESOURCEW_HPP
