#ifndef APPBOX_COMMON_WINAPI_HPP
#define APPBOX_COMMON_WINAPI_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>
#include <winternl.h>

namespace appbox
{

namespace winapi
{

/**
 * @see
 * wario.hezongjian.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessinternalw
 */
typedef BOOL (*CreateProcessInternalW)(
    HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation, PHANDLE hNewToken);

typedef HANDLE (*CreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                              DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                              HANDLE hTemplateFile);

typedef HMODULE (*LoadLibraryW)(LPCWSTR lpLibFileName);

/**
 * @see
 * https://www.geoffchappell.com/studies/windows/win32/ntdll/api/rtl/rtlexec/queryimagefileexecutionoptionsex.htm
 */
typedef NTSTATUS (*LdrQueryImageFileExecutionOptionsEx)(PUNICODE_STRING lpImageFile,
                                                        PCWSTR lpszOption, ULONG dwType,
                                                        PVOID lpData, ULONG cbData, ULONG* lpcbData,
                                                        BOOLEAN bWow64);

/**
 * @see
 * https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessmitigationpolicy
 */
typedef BOOL (*SetProcessMitigationPolicy)(PROCESS_MITIGATION_POLICY MitigationPolicy,
                                           PVOID lpBuffer, SIZE_T dwLength);

} // namespace winapi

} // namespace appbox

#endif
