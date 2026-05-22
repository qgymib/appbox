#ifndef APPBOX_SANDBOX_HOOK_CREATEPROCESSINTERNALW_HPP
#define APPBOX_SANDBOX_HOOK_CREATEPROCESSINTERNALW_HPP

#include "utils/WinAPI.h"

extern "C" {
/**
 * @see
 * http://a-twisted-world.blogspot.com/2008/03/createprocessinternal-function.html
 */
typedef BOOL (*T_CreateProcessInternalW)(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                         LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                         LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                         ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                         LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation,
                                         PHANDLE hNewToken);

/**
 * @brief CreateProcessInternalW() direct call.
 */
extern T_CreateProcessInternalW sys_CreateProcessInternalW;
}

namespace appbox
{

/**
 * @brief Inject CreateProcessInternalW() hook.
 */
void InjectCreateProcessInternalW();

} // namespace appbox

#endif
