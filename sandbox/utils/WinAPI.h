#ifndef APPBOX_SANDBOX_UTILS_WINAPI_H
#define APPBOX_SANDBOX_UTILS_WINAPI_H

#include <ntstatus.h>
#define WIN32_NO_STATUS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

#define FILE_OPEN_BY_FILE_ID 0x00002000

#ifdef __cplusplus
extern "C" {
#endif

typedef long NTSTATUS;

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
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_NAME_INFORMATION
{
    UNICODE_STRING Name;
} OBJECT_NAME_INFORMATION, *POBJECT_NAME_INFORMATION;

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

__declspec(dllimport) NTSTATUS __stdcall RtlInitUnicodeString(PUNICODE_STRING DestinationString,
                                                              const WCHAR*    SourceString);

#ifdef __cplusplus
}
#endif
#endif
