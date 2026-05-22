#ifndef APPBOX_SANDBOX_HOOK_NTCLOSE_HPP
#define APPBOX_SANDBOX_HOOK_NTCLOSE_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntclose
 */
typedef NTSTATUS (*T_NtClose)(HANDLE Handle);

/**
 * @brief NtClose() direct call.
 */
extern T_NtClose sys_NtClose;
}

namespace appbox
{

/**
 * @brief Inject NtClose() hook.
 */
void InjectNtClose();

} // namespace appbox

#endif
