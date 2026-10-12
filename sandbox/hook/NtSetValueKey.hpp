#ifndef APPBOX_SANDBOX_HOOK_NTSETVALUEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTSETVALUEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C" {
/**
 * @brief NtSetValueKey() direct call.
 *
 * The signature of the entry point is declared by `utils/WinAPI.h`, which the
 * module includes everywhere; only the pointer to the unhooked function lives
 * here.
 */
extern T_NtSetValueKey sys_NtSetValueKey;
}

namespace appbox
{

/**
 * @brief Hook NtSetValueKey().
 */
extern HookRecord HookNtSetValueKey;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTSETVALUEKEY_HPP
