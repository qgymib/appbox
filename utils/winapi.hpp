#ifndef APPBOX_UTILS_WINAPI_HPP
#define APPBOX_UTILS_WINAPI_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>
#include <winternl.h>

namespace appbox
{

namespace winapi
{

typedef BOOL (*CreateProcessInternalW)(
    HANDLE hToken, LPCWSTR lpApplicationName, LPCWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, ULONG dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation, PHANDLE hNewToken);

typedef HANDLE (*CreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                              DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                              HANDLE hTemplateFile);

typedef HMODULE (*LoadLibraryW)(LPCWSTR lpLibFileName);

typedef NTSTATUS (*NtCreateFile)(PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
                                 POBJECT_ATTRIBUTES ObjectAttributes,
                                 PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize,
                                 ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition,
                                 ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength);

#define ObjectNameInformation ((OBJECT_INFORMATION_CLASS)1)
#define ObjectAllTypesInformation ((OBJECT_INFORMATION_CLASS)3)
#define ObjectDataInformation ((OBJECT_INFORMATION_CLASS)4)

typedef NTSTATUS (*NtQueryObject)(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                  PVOID ObjectInformation, ULONG ObjectInformationLength,
                                  PULONG ReturnLength);

} // namespace winapi

} // namespace appbox

#endif
