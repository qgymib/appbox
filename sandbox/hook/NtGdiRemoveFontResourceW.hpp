#ifndef APPBOX_SANDBOX_HOOK_NTGDIREMOVEFONTRESOURCEW_HPP
#define APPBOX_SANDBOX_HOOK_NTGDIREMOVEFONTRESOURCEW_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @brief Entry point of the window manager which removes font resources.
 *
 * The prototype is the one of NtGdiAddFontResourceW, which was measured on this
 * build and is described in docs/FontsIsolation.md. The entry point has to be
 * hooked as well: a caller which removes a font by the path it added it with
 * names the path of the view, so the removal has to be rewritten like the
 * addition to name the file of the layer.
 */
/* clang-format off */
typedef int (WINAPI *T_NtGdiRemoveFontResourceW)(
    /* [IN] */ LPCWSTR   pwszFiles,
    /* [IN] */ ULONG     cwc,
    /* [IN] */ ULONG     cFiles,
    /* [IN] */ ULONG     flags,
    /* [IN] */ ULONG_PTR reserved,
    /* [IN] */ PVOID     pdv
);
/* clang-format on */

/**
 * @brief NtGdiRemoveFontResourceW() direct call.
 */
extern T_NtGdiRemoveFontResourceW sys_NtGdiRemoveFontResourceW;
}

namespace appbox
{

/**
 * @brief Hook NtGdiRemoveFontResourceW().
 */
extern HookRecord HookNtGdiRemoveFontResourceW;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTGDIREMOVEFONTRESOURCEW_HPP
