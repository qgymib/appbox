#ifndef APPBOX_SANDBOX_HOOK_NTQUERYKEY_HPP
#define APPBOX_SANDBOX_HOOK_NTQUERYKEY_HPP

#include "utils/WinAPI.h"
#include "__init__.hpp"

extern "C"
{
/**
 * @brief NtQueryKey() direct call.
 * @see The prototype T_NtQueryKey is defined in utils/WinAPI.h.
 */
extern T_NtQueryKey sys_NtQueryKey;
}

namespace appbox
{

/**
 * @brief Hook NtQueryKey().
 *
 * Key names reported for hive handles are translated back into the logical
 * view path, and the sub key / value counts of KeyFullInformation and
 * KeyCachedInformation are corrected to the merged two layer view.
 */
extern HookRecord HookNtQueryKey;

} // namespace appbox

#endif // APPBOX_SANDBOX_HOOK_NTQUERYKEY_HPP
