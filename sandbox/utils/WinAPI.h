#ifndef APPBOX_SANDBOX_UTILS_WINAPI_H
#define APPBOX_SANDBOX_UTILS_WINAPI_H

#include <ntstatus.h>
#define WIN32_NO_STATUS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

#ifndef InitializeObjectAttributes
#define InitializeObjectAttributes(p, n, a, r, s)                                                                      \
    {                                                                                                                  \
        (p)->Length = sizeof(OBJECT_ATTRIBUTES);                                                                       \
        (p)->RootDirectory = r;                                                                                        \
        (p)->Attributes = a;                                                                                           \
        (p)->ObjectName = n;                                                                                           \
        (p)->SecurityDescriptor = s;                                                                                   \
        (p)->SecurityQualityOfService = NULL;                                                                          \
    }
#endif

/* clang-format off */
#define FILE_DIRECTORY_FILE             0x00000001
#define FILE_WRITE_THROUGH              0x00000002
#define FILE_SEQUENTIAL_ONLY            0x00000004
#define FILE_NO_INTERMEDIATE_BUFFERING  0x00000008
#define FILE_SYNCHRONOUS_IO_ALERT       0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT    0x00000020
#define FILE_NON_DIRECTORY_FILE         0x00000040
#define FILE_CREATE_TREE_CONNECTION     0x00000080
#define FILE_COMPLETE_IF_OPLOCKED       0x00000100
#define FILE_NO_EA_KNOWLEDGE            0x00000200
#define FILE_RANDOM_ACCESS              0x00000800
#define FILE_DELETE_ON_CLOSE            0x00001000
#define FILE_OPEN_BY_FILE_ID            0x00002000
#define FILE_OPEN_REQUIRING_OPLOCK      0x00010000
#define FILE_OPEN_FOR_BACKUP_INTENT     0x00004000
#define FILE_RESERVE_OPFILTER           0x00100000
#define FILE_OPEN_REPARSE_POINT         0x00200000
#define FILE_OPEN_NO_RECALL             0x00400000
#define FILE_OPEN_FOR_FREE_SPACE_QUERY  0x00800000

#define FILE_SUPERSEDE                  0x00000000
#define FILE_OPEN                       0x00000001
#define FILE_CREATE                     0x00000002
#define FILE_OPEN_IF                    0x00000003
#define FILE_OVERWRITE                  0x00000004
#define FILE_OVERWRITE_IF               0x00000005

#define OBJ_CASE_INSENSITIVE            0x00000040
#define OBJ_INHERIT                     0x00000002
/* clang-format on */

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

typedef enum _FILE_INFORMATION_CLASS
{
    // end_wdm
    FileDirectoryInformation = 1,
    FileFullDirectoryInformation,            // 2
    FileBothDirectoryInformation,            // 3
    FileBasicInformation,                    // 4  wdm
    FileStandardInformation,                 // 5  wdm
    FileInternalInformation,                 // 6
    FileEaInformation,                       // 7
    FileAccessInformation,                   // 8
    FileNameInformation,                     // 9
    FileRenameInformation,                   // 10
    FileLinkInformation,                     // 11
    FileNamesInformation,                    // 12
    FileDispositionInformation,              // 13
    FilePositionInformation,                 // 14 wdm
    FileFullEaInformation,                   // 15
    FileModeInformation,                     // 16
    FileAlignmentInformation,                // 17
    FileAllInformation,                      // 18
    FileAllocationInformation,               // 19
    FileEndOfFileInformation,                // 20 wdm
    FileAlternateNameInformation,            // 21
    FileStreamInformation,                   // 22
    FilePipeInformation,                     // 23
    FilePipeLocalInformation,                // 24
    FilePipeRemoteInformation,               // 25
    FileMailslotQueryInformation,            // 26
    FileMailslotSetInformation,              // 27
    FileCompressionInformation,              // 28
    FileObjectIdInformation,                 // 29
    FileCompletionInformation,               // 30
    FileMoveClusterInformation,              // 31
    FileQuotaInformation,                    // 32
    FileReparsePointInformation,             // 33
    FileNetworkOpenInformation,              // 34
    FileAttributeTagInformation,             // 35
    FileTrackingInformation,                 // 36
    FileIdBothDirectoryInformation,          // 37
    FileIdFullDirectoryInformation,          // 38
    FileValidDataLengthInformation,          // 39
    FileShortNameInformation,                // 40
    FileIoCompletionNotificationInformation, // 41
    FileIoStatusBlockRangeInformation,       // 42
    FileIoPriorityHintInformation,           // 43
    FileSfioReserveInformation,              // 44
    FileSfioVolumeInformation,               // 45
    FileHardLinkInformation,                 // 46
    FileProcessIdsUsingFileInformation,      // 47
    FileNormalizedNameInformation,           // 48
    FileNetworkPhysicalNameInformation,      // 49
    FileIdGlobalTxDirectoryInformation,      // 50
    FileIsRemoteDeviceInformation,           // 51
    FileAttributeCacheInformation,           // 52
    FileNumaNodeInformation,                 // 53
    FileStandardLinkInformation,             // 54
    FileRemoteProtocolInformation,           // 55
    FileRenameInformationBypassAccessCheck,  // 56 - kernel mode only
    FileLinkInformationBypassAccessCheck,    // 57 - kernel mode only
    FileVolumeNameInformation,               // 58
    FileIdInformation,                       // 59
    FileIdExtdDirectoryInformation,          // 60
    FileReplaceCompletionInformation,
    FileHardLinkFullIdInformation,
    FileIdExtdBothDirectoryInformation,
    FileDispositionInformationEx,
    FileRenameInformationEx,                      // 65
    FileRenameInformationExBypassAccessCheck,     // 66 - kernel mode only
    FileDesiredStorageClassInformation,           // 67
    FileStatInformation,                          // 68
    FileMemoryPartitionInformation,               // 69
    FileStatLxInformation,                        // 70
    FileCaseSensitiveInformation,                 // 71
    FileLinkInformationEx,                        // 72
    FileLinkInformationExBypassAccessCheck,       // 73 - kernel mode only
    FileStorageReserveIdInformation,              // 74
    FileCaseSensitiveInformationForceAccessCheck, // 75

    FileMaximumInformation
} FILE_INFORMATION_CLASS, *PFILE_INFORMATION_CLASS;

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

typedef struct _FILE_BASIC_INFORMATION
{
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG         FileAttributes;
} FILE_BASIC_INFORMATION, *PFILE_BASIC_INFORMATION;

typedef struct _FILE_NETWORK_OPEN_INFORMATION
{
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG         FileAttributes;
} FILE_NETWORK_OPEN_INFORMATION, *PFILE_NETWORK_OPEN_INFORMATION;

typedef void(NTAPI* PIO_APC_ROUTINE)(IN PVOID ApcContext, IN PIO_STATUS_BLOCK IoStatusBlock, IN ULONG Reserved);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/devnotes/rtldospathnametontpathname_u_withstatus
 */
typedef NTSTATUS (*T_RtlDosPathNameToNtPathName_U_WithStatus)(PCWSTR DosFileName, PUNICODE_STRING NtFileName,
                                                              PWSTR* FilePart, PVOID Reserved);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-rtlfreeunicodestring
 */
typedef void (*T_RtlFreeUnicodeString)(PUNICODE_STRING UnicodeString);

#ifdef __cplusplus
}
#endif
#endif
