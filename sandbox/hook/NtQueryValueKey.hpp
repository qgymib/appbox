#ifndef APPBOX_SANDBOX_HOOK_NTQUERYVALUEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYVALUEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C"
{
/**
 * @brief NtQueryValueKey() direct call.
 * @see The prototype T_NtQueryValueKey is defined in utils/WinAPI.h.
 */
extern T_NtQueryValueKey sys_NtQueryValueKey;
}

namespace appbox
{

/**
 * @brief Hook NtQueryValueKey().
 *
 * A value which is missing inside the hive layer is read through from the real
 * registry, so shadow keys no longer hide the values of the real key.
 */
extern HookRecord HookNtQueryValueKey;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTQUERYVALUEKEY_HPP
