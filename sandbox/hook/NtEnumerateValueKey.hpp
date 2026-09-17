#ifndef APPBOX_SANDBOX_HOOK_NTENUMERATEVALUEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTENUMERATEVALUEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C"
{
/**
 * @brief NtEnumerateValueKey() direct call.
 * @see The prototype T_NtEnumerateValueKey is defined in utils/WinAPI.h.
 */
extern T_NtEnumerateValueKey sys_NtEnumerateValueKey;
}

namespace appbox
{

/**
 * @brief Hook NtEnumerateValueKey().
 *
 * Enumeration below a hive handle merges the values of the hive layer with the
 * values of the real key which the view path addresses: the hive layer comes
 * first and real values shadowed by hive values of the same name are skipped.
 */
extern HookRecord HookNtEnumerateValueKey;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTENUMERATEVALUEKEY_HPP
