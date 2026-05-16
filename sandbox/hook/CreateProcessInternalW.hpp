#ifndef APPBOX_SANDBOX_HOOK_CREATEPROCESSINTERNALW_HPP
#define APPBOX_SANDBOX_HOOK_CREATEPROCESSINTERNALW_HPP

#include "utils/WinAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @see
 * wario.hezongjian.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessinternalw
 */
typedef BOOL (*T_CreateProcessInternalW)(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                         LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                         LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                         ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
                                         LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation,
                                         PHANDLE hNewToken);

extern T_CreateProcessInternalW sys_CreateProcessInternalW;

#ifdef __cplusplus
}
#endif

namespace appbox
{

void InjectCreateProcessInternalW();

}

#endif
