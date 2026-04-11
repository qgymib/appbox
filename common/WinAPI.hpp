#ifndef APPBOX_COMMON_WINAPI_HPP
#define APPBOX_COMMON_WINAPI_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _OBJECT_INFORMATION_CLASS
{
    ObjectBasicInformation,
    ObjectNameInformation,
    ObjectTypeInformation,
    ObjectAllTypesInformation,
    ObjectDataInformation
} OBJECT_INFORMATION_CLASS;

typedef struct _UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING;
typedef UNICODE_STRING* PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES
{
    ULONG           Length;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
    PVOID           SecurityDescriptor;       // Points to type SECURITY_DESCRIPTOR
    PVOID           SecurityQualityOfService; // Points to type SECURITY_QUALITY_OF_SERVICE
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _IO_STATUS_BLOCK
{
    union {
        NTSTATUS Status;
        PVOID    Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

#ifdef __cplusplus
}
#endif

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

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile
 */
typedef NTSTATUS (*NtCreateFile)(PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
                                 POBJECT_ATTRIBUTES ObjectAttributes,
                                 PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize,
                                 ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition,
                                 ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject
 */
typedef NTSTATUS (*NtQueryObject)(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                  PVOID ObjectInformation, ULONG ObjectInformationLength,
                                  PULONG ReturnLength);

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
