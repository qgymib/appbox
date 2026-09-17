#ifndef APPBOX_SANDBOX_HOOK_NTENUMERATEKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTENUMERATEKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C"
{
/**
 * @brief NtEnumerateKey() direct call.
 * @see The prototype T_NtEnumerateKey is defined in utils/WinAPI.h.
 */
extern T_NtEnumerateKey sys_NtEnumerateKey;
}

namespace appbox
{

/**
 * @brief Hook NtEnumerateKey().
 *
 * Enumeration below a hive handle merges the sub keys of the hive layer with
 * the sub keys of the real key which the view path addresses: the hive layer
 * comes first and real sub keys shadowed by hive keys of the same name are
 * skipped.
 */
extern HookRecord HookNtEnumerateKey;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTENUMERATEKEY_HPP
