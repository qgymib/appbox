#ifndef APPBOX_SANDBOX_UTILS_WINAPI_H
#define APPBOX_SANDBOX_UTILS_WINAPI_H

#include <ntstatus.h>
#define WIN32_NO_STATUS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ntdef/nf-ntdef-initializeobjectattributes
 */
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
#define FILE_DIRECTORY_FILE                         0x00000001
#define FILE_WRITE_THROUGH                          0x00000002
#define FILE_SEQUENTIAL_ONLY                        0x00000004
#define FILE_NO_INTERMEDIATE_BUFFERING              0x00000008
#define FILE_SYNCHRONOUS_IO_ALERT                   0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT                0x00000020
#define FILE_NON_DIRECTORY_FILE                     0x00000040
#define FILE_CREATE_TREE_CONNECTION                 0x00000080
#define FILE_COMPLETE_IF_OPLOCKED                   0x00000100
#define FILE_NO_EA_KNOWLEDGE                        0x00000200
#define FILE_RANDOM_ACCESS                          0x00000800
#define FILE_DELETE_ON_CLOSE                        0x00001000
#define FILE_OPEN_BY_FILE_ID                        0x00002000
#define FILE_OPEN_REQUIRING_OPLOCK                  0x00010000
#define FILE_OPEN_FOR_BACKUP_INTENT                 0x00004000
#define FILE_RESERVE_OPFILTER                       0x00100000
#define FILE_OPEN_REPARSE_POINT                     0x00200000
#define FILE_OPEN_NO_RECALL                         0x00400000
#define FILE_OPEN_FOR_FREE_SPACE_QUERY              0x00800000

#define FILE_SUPERSEDE                              0x00000000
#define FILE_OPEN                                   0x00000001
#define FILE_CREATE                                 0x00000002
#define FILE_OPEN_IF                                0x00000003
#define FILE_OVERWRITE                              0x00000004
#define FILE_OVERWRITE_IF                           0x00000005

#define FILE_DISPOSITION_DO_NOT_DELETE              0x00000000
#define FILE_DISPOSITION_DELETE                     0x00000001
#define FILE_DISPOSITION_POSIX_SEMANTICS            0x00000002
#define FILE_DISPOSITION_FORCE_IMAGE_SECTION_CHECK  0x00000004
#define FILE_DISPOSITION_ON_CLOSE                   0x00000008
#define FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE  0x00000010

#define OBJ_CASE_INSENSITIVE                        0x00000040
#define OBJ_INHERIT                                 0x00000002

/*
 * The flags of the `...Ex` classes of a rename and of a link. The Windows SDK
 * declares the rename flags below `_WIN32_WINNT_WIN10_RS1`, which this module
 * does not ask for, so they are declared here as well.
 */
#ifndef FILE_RENAME_FLAG_REPLACE_IF_EXISTS
#define FILE_RENAME_FLAG_REPLACE_IF_EXISTS          0x00000001
#endif
#ifndef FILE_RENAME_FLAG_POSIX_SEMANTICS
#define FILE_RENAME_FLAG_POSIX_SEMANTICS            0x00000002
#endif
#ifndef FILE_LINK_FLAG_REPLACE_IF_EXISTS
#define FILE_LINK_FLAG_REPLACE_IF_EXISTS            0x00000001
#endif

#define SL_RESTART_SCAN                             0x00000001
#define SL_RETURN_SINGLE_ENTRY                      0x00000002
#define SL_INDEX_SPECIFIED                          0x00000004
#define SL_RETURN_ON_DISK_ENTRIES_ONLY              0x00000008
#define SL_NO_CURSOR_UPDATE_QUERY                   0x00000010
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _NTDEF_
typedef long      NTSTATUS;
typedef NTSTATUS* PNTSTATUS;
#define _NTDEF_
#endif

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

/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_fsinfoclass
 */
typedef enum _FSINFOCLASS
{
    FileFsVolumeInformation,
    FileFsLabelInformation,
    FileFsSizeInformation,
    FileFsDeviceInformation,
    FileFsAttributeInformation,
    FileFsControlInformation,
    FileFsFullSizeInformation,
    FileFsObjectIdInformation,
    FileFsDriverPathInformation,
    FileFsVolumeFlagsInformation,
    FileFsSectorSizeInformation,
    FileFsDataCopyInformation,
    FileFsMetadataSizeInformation,
    FileFsFullSizeInformationEx,
    FileFsGuidInformation,
    FileFsMaximumInformation
} FS_INFORMATION_CLASS, *PFS_INFORMATION_CLASS;

/**
 * @see https://ntdoc.m417z.com/processinfoclass
 */
typedef enum _PROCESSINFOCLASS
{
    ProcessBasicInformation,          // q: PROCESS_BASIC_INFORMATION, PROCESS_EXTENDED_BASIC_INFORMATION
    ProcessQuotaLimits,               // qs: QUOTA_LIMITS, QUOTA_LIMITS_EX
    ProcessIoCounters,                // q: IO_COUNTERS
    ProcessVmCounters,                // q: VM_COUNTERS, VM_COUNTERS_EX, VM_COUNTERS_EX2
    ProcessTimes,                     // q: KERNEL_USER_TIMES // since VISTA
    ProcessBasePriority,              // s: KPRIORITY
    ProcessRaisePriority,             // s: PROCESS_RAISE_PRIORITY
    ProcessDebugPort,                 // q: HANDLE
    ProcessExceptionPort,             // s: PROCESS_EXCEPTION_PORT (requires SeTcbPrivilege)
    ProcessAccessToken,               // s: PROCESS_ACCESS_TOKEN
    ProcessLdtInformation,            // qs: PROCESS_LDT_INFORMATION // 10
    ProcessLdtSize,                   // s: PROCESS_LDT_SIZE
    ProcessDefaultHardErrorMode,      // qs: PROCESS_DEFAULT_HARD_ERROR_MODE
    ProcessIoPortHandlers,            // s: PROCESS_IO_PORT_HANDLER_INFORMATION // (kernel-mode only)
    ProcessPooledUsageAndLimits,      // q: POOLED_USAGE_AND_LIMITS
    ProcessWorkingSetWatch,           // qs: PROCESS_WS_WATCH_INFORMATION[]; s: void
    ProcessUserModeIOPL,              // s: PROCESS_USER_MODE_IOPL (requires SeTcbPrivilege)
    ProcessEnableAlignmentFaultFixup, // s: BOOLEAN
    ProcessPriorityClass,             // qs: PROCESS_PRIORITY_CLASS
    ProcessWx86Information,           // qs: ULONG (requires SeTcbPrivilege) (VdmAllowed)
    ProcessHandleCount,               // q: ULONG, PROCESS_HANDLE_INFORMATION // 20
    ProcessAffinityMask,              // qs: KAFFINITY, qs: GROUP_AFFINITY
    ProcessPriorityBoost,             // qs: PROCESS_PRIORITY_BOOST
    ProcessDeviceMap,                 // qs: PROCESS_DEVICEMAP_INFORMATION, PROCESS_DEVICEMAP_INFORMATION_EX
    ProcessSessionInformation,        // qs: PROCESS_SESSION_INFORMATION
    ProcessForegroundInformation,     // s: PROCESS_FOREGROUND_BACKGROUND
    ProcessWow64Information,          // q: ULONG_PTR
    ProcessImageFileName,             // q: UNICODE_STRING
    ProcessLUIDDeviceMapsEnabled,     // q: PROCESS_LUID_DEVICE_MAPS_ENABLED
    ProcessBreakOnTermination,        // qs: ULONG
    ProcessDebugObjectHandle,         // q: HANDLE // 30
    ProcessDebugFlags,                // qs: PROCESS_DEBUG_FLAGS
    ProcessHandleTracing,  // qs: PROCESS_HANDLE_TRACING_QUERY; s: PROCESS_HANDLE_TRACING_ENABLE[_EX] or void to disable
    ProcessIoPriority,     // qs: IO_PRIORITY_HINT (s: requires SeIncreaseBasePriorityPrivilege)
    ProcessExecuteFlags,   // qs: PROCESS_EXECUTE_FLAGS
    ProcessTlsInformation, // s: PROCESS_TLS_INFORMATION // ProcessResourceManagement
    ProcessCookie,         // q: ULONG
    ProcessImageInformation,        // q: SECTION_IMAGE_INFORMATION
    ProcessCycleTime,               // q: PROCESS_CYCLE_TIME_INFORMATION // since VISTA
    ProcessPagePriority,            // qs: PAGE_PRIORITY_INFORMATION
    ProcessInstrumentationCallback, // s: PVOID or PROCESS_INSTRUMENTATION_CALLBACK_INFORMATION // 40
    ProcessThreadStackAllocation,   // s: PROCESS_STACK_ALLOCATION_INFORMATION, PROCESS_STACK_ALLOCATION_INFORMATION_EX
    ProcessWorkingSetWatchEx,       // qs: PROCESS_WS_WATCH_INFORMATION_EX[]; s: void
    ProcessImageFileNameWin32,      // q: UNICODE_STRING
    ProcessImageFileMapping,        // q: HANDLE (input)
    ProcessAffinityUpdateMode,      // qs: PROCESS_AFFINITY_UPDATE_MODE
    ProcessMemoryAllocationMode,    // qs: PROCESS_MEMORY_ALLOCATION_MODE
    ProcessGroupInformation,        // q: PROCESS_GROUP_INFORMATION
    ProcessTokenVirtualizationEnabled,           // s: ULONG
    ProcessConsoleHostProcess,                   // qs: PROCESS_CONSOLE_HOST_PROCESS_INFORMATION
    ProcessWindowInformation,                    // q: PROCESS_WINDOW_INFORMATION // 50
    ProcessHandleInformation,                    // q: PROCESS_HANDLE_SNAPSHOT_INFORMATION // since WIN8
    ProcessMitigationPolicy,                     // s: PROCESS_MITIGATION_POLICY_INFORMATION
    ProcessDynamicFunctionTableInformation,      // s: PROCESS_DYNAMIC_FUNCTION_TABLE_INFORMATION
    ProcessHandleCheckingMode,                   // qs: PROCESS_HANDLE_CHECKING_MODE; s: 0 disables, otherwise enables
    ProcessKeepAliveCount,                       // q: PROCESS_KEEPALIVE_COUNT_INFORMATION
    ProcessRevokeFileHandles,                    // s: PROCESS_REVOKE_FILE_HANDLES_INFORMATION
    ProcessWorkingSetControl,                    // s: PROCESS_WORKING_SET_CONTROL
    ProcessHandleTable,                          // q: ULONG[] // since WINBLUE
    ProcessCheckStackExtentsMode,                // qs: ULONG // KPROCESS->CheckStackExtents (CFG)
    ProcessCommandLineInformation,               // q: UNICODE_STRING // 60
    ProcessProtectionInformation,                // q: PS_PROTECTION
    ProcessMemoryExhaustion,                     // s: PROCESS_MEMORY_EXHAUSTION_INFO // since THRESHOLD
    ProcessFaultInformation,                     // s: PROCESS_FAULT_INFORMATION
    ProcessTelemetryIdInformation,               // q: PROCESS_TELEMETRY_ID_INFORMATION
    ProcessCommitReleaseInformation,             // qs: PROCESS_COMMIT_RELEASE_INFORMATION
    ProcessDefaultCpuSetsInformation,            // qs: SYSTEM_CPU_SET_INFORMATION[5] // ProcessReserved1Information
    ProcessAllowedCpuSetsInformation,            // qs: SYSTEM_CPU_SET_INFORMATION[5] // ProcessReserved2Information
    ProcessSubsystemProcess,                     // s: void // EPROCESS->SubsystemProcess
    ProcessJobMemoryInformation,                 // q: PROCESS_JOB_MEMORY_INFO
    ProcessInPrivate,                            // qs: BOOLEAN; s: void // ETW // since THRESHOLD2 // 70
    ProcessRaiseUMExceptionOnInvalidHandleClose, // qs: PROCESS_RAISE_UM_EXCEPTION_ON_INVALID_HANDLE_CLOSE; s: 0
                                                 // disables, otherwise enables
    ProcessIumChallengeResponse,                 // qs: PROCESS_IUM_CHALLENGE_RESPONSE
    ProcessChildProcessInformation,              // q: PROCESS_CHILD_PROCESS_INFORMATION
    ProcessHighGraphicsPriorityInformation,      // qs: BOOLEAN; s: BOOLEAN (requires SeTcbPrivilege)
    ProcessSubsystemInformation,                 // q: SUBSYSTEM_INFORMATION_TYPE // since REDSTONE2
    ProcessEnergyValues, // q: PROCESS_ENERGY_VALUES, PROCESS_EXTENDED_ENERGY_VALUES, PROCESS_EXTENDED_ENERGY_VALUES_V1
    ProcessPowerThrottlingState,   // qs: POWER_THROTTLING_PROCESS_STATE
    ProcessActivityThrottlePolicy, // qs: Obsolete // PROCESS_ACTIVITY_THROTTLE_POLICY // ProcessReserved3Information
    ProcessWin32kSyscallFilterInformation,     // q: WIN32K_SYSCALL_FILTER
    ProcessDisableSystemAllowedCpuSets,        // s: BOOLEAN // 80
    ProcessWakeInformation,                    // q: PROCESS_WAKE_INFORMATION // (kernel-mode only)
    ProcessEnergyTrackingState,                // qs: PROCESS_ENERGY_TRACKING_STATE
    ProcessManageWritesToExecutableMemory,     // s: MANAGE_WRITES_TO_EXECUTABLE_MEMORY // since REDSTONE3
    ProcessCaptureTrustletLiveDump,            // q: ULONG
    ProcessTelemetryCoverage,                  // qs: TELEMETRY_COVERAGE_HEADER; s: TELEMETRY_COVERAGE_POINT
    ProcessEnclaveInformation,                 // qs: Obsolete
    ProcessEnableReadWriteVmLogging,           // qs: PROCESS_READWRITEVM_LOGGING_INFORMATION
    ProcessUptimeInformation,                  // q: PROCESS_UPTIME_INFORMATION
    ProcessImageSection,                       // q: HANDLE
    ProcessDebugAuthInformation,               // s: PROCESS_DEBUG_AUTH_INFORMATION // CiTool.exe -- device-id //
                                               // PplDebugAuthorization // since RS4 // 90
    ProcessSystemResourceManagement,           // s: PROCESS_SYSTEM_RESOURCE_MANAGEMENT
    ProcessSequenceNumber,                     // q: ULONGLONG
    ProcessLoaderDetour,                       // qs: Obsolete // since RS5
    ProcessSecurityDomainInformation,          // q: PROCESS_SECURITY_DOMAIN_INFORMATION
    ProcessCombineSecurityDomainsInformation,  // s: PROCESS_COMBINE_SECURITY_DOMAINS_INFORMATION
    ProcessEnableLogging,                      // q: PROCESS_LOGGING_INFORMATION
    ProcessLeapSecondInformation,              // qs: PROCESS_LEAP_SECOND_INFORMATION
    ProcessFiberShadowStackAllocation,         // s: PROCESS_FIBER_SHADOW_STACK_ALLOCATION_INFORMATION // since 19H1
    ProcessFreeFiberShadowStackAllocation,     // s: PROCESS_FREE_FIBER_SHADOW_STACK_ALLOCATION_INFORMATION
    ProcessAltSystemCallInformation,           // s: PROCESS_SYSCALL_PROVIDER_INFORMATION // since 20H1 // 100
    ProcessDynamicEHContinuationTargets,       // s: PROCESS_DYNAMIC_EH_CONTINUATION_TARGETS_INFORMATION
    ProcessDynamicEnforcedCetCompatibleRanges, // s: PROCESS_DYNAMIC_ENFORCED_ADDRESS_RANGE_INFORMATION // since 20H2
    ProcessCreateStateChange,                  // qs: Obsolete // since WIN11
    ProcessApplyStateChange,                   // qs: Obsolete
    ProcessEnableOptionalXStateFeatures,       // s: ULONG64 // EnableProcessOptionalXStateFeatures
    ProcessAltPrefetchParam,          // qs: OVERRIDE_PREFETCH_PARAMETER // App Launch Prefetch (ALPF) // since 22H1
    ProcessAssignCpuPartitions,       // s: HANDLE[]
    ProcessPriorityClassEx,           // s: PROCESS_PRIORITY_CLASS_EX
    ProcessMembershipInformation,     // q: PROCESS_MEMBERSHIP_INFORMATION
    ProcessEffectiveIoPriority,       // q: IO_PRIORITY_HINT // 110
    ProcessEffectivePagePriority,     // q: ULONG
    ProcessSchedulerSharedData,       // s: PROCESS_SCHEDULER_SHARED_DATA_SLOT_INFORMATION // since 24H2
    ProcessSlistRollbackInformation,  // qs: no input buffer, length 0 on set, current process only
    ProcessNetworkIoCounters,         // q: PROCESS_NETWORK_COUNTERS
    ProcessFindFirstThreadByTebValue, // q: PROCESS_TEB_VALUE_INFORMATION // NtCurrentProcess
    ProcessEnclaveAddressSpaceRestriction, // qs: Obsolete // since 25H2
    ProcessAvailableCpus,                  // qs: Obsolete // PROCESS_AVAILABLE_CPUS_INFORMATION
    MaxProcessInfoClass
} PROCESSINFOCLASS;

typedef struct _UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;

/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_key_information_class
 */
#ifndef _KEY_INFORMATION_CLASS
#define _KEY_INFORMATION_CLASS
typedef enum _KEY_INFORMATION_CLASS
{
    KeyBasicInformation,
    KeyNodeInformation,
    KeyFullInformation,
    KeyNameInformation,
    KeyCachedInformation,
    KeyFlagsInformation,
    KeyVirtualizationInformation,
    KeyHandleTagsInformation,
    KeyTrustInformation,
    KeyLayerInformation,
    MaximumKeyInfoClass
} KEY_INFORMATION_CLASS, *PKEY_INFORMATION_CLASS;
#endif

/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_key_value_information_class
 */
#ifndef _KEY_VALUE_INFORMATION_CLASS
#define _KEY_VALUE_INFORMATION_CLASS
typedef enum _KEY_VALUE_INFORMATION_CLASS
{
    KeyValueBasicInformation,
    KeyValueFullInformation,
    KeyValuePartialInformation,
    KeyValueFullInformationAlign64,
    KeyValuePartialInformationAlign64,
    KeyValueLayerInformation,
    MaximumKeyValueInfoClass
} KEY_VALUE_INFORMATION_CLASS, *PKEY_VALUE_INFORMATION_CLASS;
#endif

typedef struct _KEY_BASIC_INFORMATION
{
    LARGE_INTEGER LastWriteTime;
    ULONG         TitleIndex;
    ULONG         NameLength;
    WCHAR         Name[1];
} KEY_BASIC_INFORMATION, *PKEY_BASIC_INFORMATION;

typedef struct _KEY_NODE_INFORMATION
{
    LARGE_INTEGER LastWriteTime;
    ULONG         TitleIndex;
    ULONG         ClassOffset;
    ULONG         ClassLength;
    ULONG         NameLength;
    WCHAR         Name[1];
} KEY_NODE_INFORMATION, *PKEY_NODE_INFORMATION;

typedef struct _KEY_FULL_INFORMATION
{
    LARGE_INTEGER LastWriteTime;
    ULONG         TitleIndex;
    ULONG         ClassOffset;
    ULONG         ClassLength;
    ULONG         SubKeys;
    ULONG         MaxNameLen;
    ULONG         MaxClassLen;
    ULONG         Values;
    ULONG         MaxValueNameLen;
    ULONG         MaxValueDataLen;
    WCHAR         Class[1];
} KEY_FULL_INFORMATION, *PKEY_FULL_INFORMATION;

typedef struct _KEY_NAME_INFORMATION
{
    ULONG NameLength;
    WCHAR Name[1];
} KEY_NAME_INFORMATION, *PKEY_NAME_INFORMATION;

typedef struct _KEY_CACHED_INFORMATION
{
    LARGE_INTEGER LastWriteTime;
    ULONG         TitleIndex;
    ULONG         SubKeys;
    ULONG         MaxNameLen;
    ULONG         Values;
    ULONG         MaxValueNameLen;
    ULONG         MaxValueDataLen;
    ULONG         NameType;
    ULONG         Flags;
} KEY_CACHED_INFORMATION, *PKEY_CACHED_INFORMATION;

typedef struct _KEY_VALUE_BASIC_INFORMATION
{
    ULONG TitleIndex;
    ULONG Type;
    ULONG NameLength;
    WCHAR Name[1];
} KEY_VALUE_BASIC_INFORMATION, *PKEY_VALUE_BASIC_INFORMATION;

typedef struct _KEY_VALUE_PARTIAL_INFORMATION
{
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataLength;
    UCHAR Data[1];
} KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;

typedef struct _KEY_VALUE_PARTIAL_INFORMATION_ALIGN64
{
    ULONG Type;
    ULONG DataLength;
    UCHAR Data[1];
} KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, *PKEY_VALUE_PARTIAL_INFORMATION_ALIGN64;

typedef struct _KEY_VALUE_FULL_INFORMATION
{
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataOffset;
    ULONG DataLength;
    ULONG NameLength;
    WCHAR Name[1];
} KEY_VALUE_FULL_INFORMATION, *PKEY_VALUE_FULL_INFORMATION;

/**
 * @brief One value of a multi value query.
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerymultiplevaluekey
 */
typedef struct _KEY_VALUE_ENTRY
{
    PUNICODE_STRING ValueName;
    ULONG           DataLength;
    ULONG           DataOffset;
    ULONG           Type;
} KEY_VALUE_ENTRY, *PKEY_VALUE_ENTRY;

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

typedef struct _FILE_DIRECTORY_INFORMATION
{
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    WCHAR         FileName[1];
} FILE_DIRECTORY_INFORMATION, *PFILE_DIRECTORY_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION
{
    BOOLEAN DeleteFileOnClose;
} FILE_DISPOSITION_INFORMATION, *PFILE_DISPOSITION_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION_EX
{
    ULONG Flags;
} FILE_DISPOSITION_INFORMATION_EX, *PFILE_DISPOSITION_INFORMATION_EX;

/**
 * @brief Name of an entry, reported by the name carrying query classes.
 *
 * The name is relative to the root of the volume of the handle, which is the
 * form the file system stores for a file object: a file below
 * `\??\C:\Windows` is reported as `\Windows\...`.
 *
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_name_information
 */
typedef struct _FILE_NAME_INFORMATION
{
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFORMATION, *PFILE_NAME_INFORMATION;

/**
 * @brief New name of a rename, carried by `FileRenameInformation`.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information
 */
typedef struct _FILE_RENAME_INFORMATION
{
    BOOLEAN ReplaceIfExists;
    HANDLE  RootDirectory;
    ULONG   FileNameLength;
    WCHAR   FileName[1];
} FILE_RENAME_INFORMATION, *PFILE_RENAME_INFORMATION;

/**
 * @brief New name of a rename, carried by `FileRenameInformationEx`.
 *
 * The flags of the extended form replace the `ReplaceIfExists` byte of the
 * plain form and the rest of the record has the same layout, so a caller which
 * reads the name of either class can use the same offsets.
 *
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information_ex
 */
typedef struct _FILE_RENAME_INFORMATION_EX
{
    ULONG  Flags;
    HANDLE RootDirectory;
    ULONG  FileNameLength;
    WCHAR  FileName[1];
} FILE_RENAME_INFORMATION_EX, *PFILE_RENAME_INFORMATION_EX;

/**
 * @brief New name of a hard link, carried by `FileLinkInformation`.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_link_information
 */
typedef struct _FILE_LINK_INFORMATION
{
    BOOLEAN ReplaceIfExists;
    HANDLE  RootDirectory;
    ULONG   FileNameLength;
    WCHAR   FileName[1];
} FILE_LINK_INFORMATION, *PFILE_LINK_INFORMATION;

/**
 * @brief New name of a hard link, carried by `FileLinkInformationEx`.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_link_information_ex
 */
typedef struct _FILE_LINK_INFORMATION_EX
{
    ULONG  Flags;
    HANDLE RootDirectory;
    ULONG  FileNameLength;
    WCHAR  FileName[1];
} FILE_LINK_INFORMATION_EX, *PFILE_LINK_INFORMATION_EX;

/**
 * @brief Identity of an entry inside its volume.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_internal_information
 */
typedef struct _FILE_INTERNAL_INFORMATION
{
    LARGE_INTEGER IndexNumber;
} FILE_INTERNAL_INFORMATION, *PFILE_INTERNAL_INFORMATION;

/**
 * @brief Size of the extended attributes of an entry.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_ea_information
 */
typedef struct _FILE_EA_INFORMATION
{
    ULONG EaSize;
} FILE_EA_INFORMATION, *PFILE_EA_INFORMATION;

/**
 * @brief Access mask the entry was opened with.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_access_information
 */
typedef struct _FILE_ACCESS_INFORMATION
{
    ACCESS_MASK AccessFlags;
} FILE_ACCESS_INFORMATION, *PFILE_ACCESS_INFORMATION;

/**
 * @brief Position of the file pointer of an entry.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_position_information
 */
typedef struct _FILE_POSITION_INFORMATION
{
    LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;

/**
 * @brief Mode of an entry.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_mode_information
 */
typedef struct _FILE_MODE_INFORMATION
{
    ULONG Mode;
} FILE_MODE_INFORMATION, *PFILE_MODE_INFORMATION;

/**
 * @brief Alignment an entry requires.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_alignment_information
 */
typedef struct _FILE_ALIGNMENT_INFORMATION
{
    ULONG AlignmentRequirement;
} FILE_ALIGNMENT_INFORMATION, *PFILE_ALIGNMENT_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION
{
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG         NumberOfLinks;
    BOOLEAN       DeletePending;
    BOOLEAN       Directory;
} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

/**
 * @brief Every information a handle reports, carried by `FileAllInformation`.
 *
 * The name is the last member of the record, so a caller which answers the
 * class with a translated name only has to rewrite the tail of the buffer the
 * file system filled. The `static_assert` in the hook which uses the record
 * pins the offset of the name against the layout the file system reports.
 *
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_all_information
 */
typedef struct _FILE_ALL_INFORMATION
{
    FILE_BASIC_INFORMATION     BasicInformation;
    FILE_STANDARD_INFORMATION  StandardInformation;
    FILE_INTERNAL_INFORMATION  InternalInformation;
    FILE_EA_INFORMATION        EaInformation;
    FILE_ACCESS_INFORMATION    AccessInformation;
    FILE_POSITION_INFORMATION  PositionInformation;
    FILE_MODE_INFORMATION      ModeInformation;
    FILE_ALIGNMENT_INFORMATION AlignmentInformation;
    FILE_NAME_INFORMATION      NameInformation;
} FILE_ALL_INFORMATION, *PFILE_ALL_INFORMATION;

typedef struct _PROCESS_BASIC_INFORMATION
{
    NTSTATUS  ExitStatus;
    PVOID     PebBaseAddress; // was type PPEB
    ULONG_PTR AffinityMask;
    LONG      BasePriority; // was type KPRIORITY
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;

typedef struct _RTL_USER_PROCESS_PARAMETERS
{
    BYTE           Reserved1[16];
    PVOID          Reserved2[10];
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

typedef struct _PEB
{
    BYTE                         Reserved1[2];
    BYTE                         BeingDebugged;
    BYTE                         Reserved2[1];
    PVOID                        Reserved3[2];
    PVOID                        Ldr; // was type PPEB_LDR_DATA
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
    PVOID                        Reserved4[3];
    PVOID                        AtlThunkSListPtr;
    PVOID                        Reserved5;
    ULONG                        Reserved6;
    PVOID                        Reserved7;
    ULONG                        Reserved8;
    ULONG                        AtlThunkSListPtr32;
    PVOID                        Reserved9[45];
    BYTE                         Reserved10[96];
    PVOID                        PostProcessInitRoutine; // was type PPS_POST_PROCESS_INIT_ROUTINE
    BYTE                         Reserved11[128];
    PVOID                        Reserved12[1];
    ULONG                        SessionId;
} PEB, *PPEB;

typedef struct _TEB
{
    PVOID Reserved1[12];
    PPEB  ProcessEnvironmentBlock;
    PVOID Reserved2[399];
    BYTE  Reserved3[1952];
    PVOID TlsSlots[64];
    BYTE  Reserved4[8];
    PVOID Reserved5[26];
    PVOID ReservedForOle; // Windows 2000 only
    PVOID Reserved6[4];
    PVOID TlsExpansionSlots;
} TEB, *PTEB;

typedef struct _FILE_FULL_DIR_INFORMATION
{
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    ULONG         EaSize;
    WCHAR         FileName[1];
} FILE_FULL_DIR_INFORMATION, *PFILE_FULL_DIR_INFORMATION;

typedef struct _FILE_BOTH_DIR_INFORMATION
{
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    ULONG         EaSize;
    CCHAR         ShortNameLength;
    WCHAR         ShortName[12];
    WCHAR         FileName[1];
} FILE_BOTH_DIR_INFORMATION, *PFILE_BOTH_DIR_INFORMATION;

/**
 * @brief Name of an entry, without any other property.
 *
 * The record carries the name alone, so the merge of the layers reads and
 * filters it like the records which carry more of the entry.
 *
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_names_information
 */
typedef struct _FILE_NAMES_INFORMATION
{
    ULONG NextEntryOffset;
    ULONG FileIndex;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAMES_INFORMATION, *PFILE_NAMES_INFORMATION;

/**
 * @brief Entry of a directory together with the identity of the entry.
 *
 * The identity follows the short name of the entry and is followed by the
 * name, which is the last member of the record: a caller which reads the name
 * of an entry only has to know where the name begins.
 *
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_both_dir_information
 */
typedef struct _FILE_ID_BOTH_DIR_INFORMATION
{
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    ULONG         EaSize;
    CCHAR         ShortNameLength;
    WCHAR         ShortName[12];
    LARGE_INTEGER FileId;
    WCHAR         FileName[1];
} FILE_ID_BOTH_DIR_INFORMATION, *PFILE_ID_BOTH_DIR_INFORMATION;

/**
 * @brief Entry of a directory together with the identity of the entry.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_full_dir_information
 */
typedef struct _FILE_ID_FULL_DIR_INFORMATION
{
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    ULONG         EaSize;
    LARGE_INTEGER FileId;
    WCHAR         FileName[1];
} FILE_ID_FULL_DIR_INFORMATION, *PFILE_ID_FULL_DIR_INFORMATION;

/**
 * @brief Entry of a directory together with its 128 bit identity.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_extd_dir_information
 */
typedef struct _FILE_ID_EXTD_DIR_INFORMATION
{
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    ULONG         EaSize;
    ULONG         ReparsePointTag;
    FILE_ID_128   FileId;
    WCHAR         FileName[1];
} FILE_ID_EXTD_DIR_INFORMATION, *PFILE_ID_EXTD_DIR_INFORMATION;

/**
 * @brief Entry of a directory together with its 128 bit identity and its short name.
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_extd_both_dir_information
 */
typedef struct _FILE_ID_EXTD_BOTH_DIR_INFORMATION
{
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    ULONG         EaSize;
    ULONG         ReparsePointTag;
    FILE_ID_128   FileId;
    CCHAR         ShortNameLength;
    WCHAR         ShortName[12];
    WCHAR         FileName[1];
} FILE_ID_EXTD_BOTH_DIR_INFORMATION, *PFILE_ID_EXTD_BOTH_DIR_INFORMATION;

/**
 * @brief Entry of a directory together with the transactional visibility of the entry.
 *
 * The record carries the name of the entry at a fixed offset like the other
 * records which report an entry of a directory, so the merge of the layers
 * reads and filters it the same way. The file system reports the record for a
 * directory handle which is not part of a transaction as well, with the flags
 * of the record naming no transaction.
 *
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_id_global_tx_dir_information
 */
typedef struct _FILE_ID_GLOBAL_TX_DIR_INFORMATION
{
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    LARGE_INTEGER FileId;
    GUID          LockingTransactionId;
    ULONG         TxInfoFlags;
    WCHAR         FileName[1];
} FILE_ID_GLOBAL_TX_DIR_INFORMATION, *PFILE_ID_GLOBAL_TX_DIR_INFORMATION;

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

/**
 * @see https://learn.microsoft.com/zh-cn/windows/win32/api/winternl/nf-winternl-rtlntstatustodoserror
 */
typedef ULONG (*T_RtlNtStatusToDosError)(NTSTATUS Status);

/**
 * @see https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryinformationprocess
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryInformationProcess)(
	/* [IN] */				HANDLE           	ProcessHandle,
	/* [IN] */				PROCESSINFOCLASS	ProcessInformationClass,
	/* [OUT] */				PVOID            	ProcessInformation,
	/* [IN] */				ULONG            	ProcessInformationLength,
	/* [OUT,OPTIONAL] */	PULONG           	ReturnLength
);
/* clang-format on */

/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwenumeratekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtEnumerateKey)(
	/* [IN] */  HANDLE                  KeyHandle,
	/* [IN] */  ULONG                   Index,
	/* [IN] */  KEY_INFORMATION_CLASS   KeyInformationClass,
	/* [OUT] */ PVOID                   KeyInformation,
	/* [IN] */  ULONG                   Length,
	/* [OUT] */ PULONG                  ResultLength
);
/* clang-format on */

/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwenumeratevaluekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtEnumerateValueKey)(
	/* [IN] */  HANDLE                      KeyHandle,
	/* [IN] */  ULONG                       Index,
	/* [IN] */  KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
	/* [OUT] */ PVOID                       KeyValueInformation,
	/* [IN] */  ULONG                       Length,
	/* [OUT] */ PULONG                      ResultLength
);
/* clang-format on */

/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwquerykey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryKey)(
	/* [IN] */  HANDLE                  KeyHandle,
	/* [IN] */  KEY_INFORMATION_CLASS   KeyInformationClass,
	/* [OUT] */ PVOID                   KeyInformation,
	/* [IN] */  ULONG                   Length,
	/* [OUT] */ PULONG                  ResultLength
);
/* clang-format on */

/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwqueryvaluekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtQueryValueKey)(
	/* [IN] */  HANDLE                      KeyHandle,
	/* [IN] */  PUNICODE_STRING             ValueName,
	/* [IN] */  KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
	/* [OUT] */ PVOID                       KeyValueInformation,
	/* [IN] */  ULONG                       Length,
	/* [OUT] */ PULONG                      ResultLength
);
/* clang-format on */

/**
 * @see https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwsetvaluekey
 */
/* clang-format off */
typedef NTSTATUS (*T_NtSetValueKey)(
	/* [IN] */ HANDLE          KeyHandle,
	/* [IN] */ PUNICODE_STRING ValueName,
	/* [IN] */ ULONG           TitleIndex,
	/* [IN] */ ULONG           Type,
	/* [IN] */ PVOID           Data,
	/* [IN] */ ULONG           DataSize
);
/* clang-format on */

/**
 * @brief Address families, name spaces and flags of the name resolution.
 *
 * The hooks of the network isolation live in `ws2_32.dll` and `dnsapi.dll`. The
 * winsock 2 and the DNS headers cannot be included next to the winsock header
 * of <windows.h>, so the few constants and structures those hooks need are
 * declared here.
 * @{
 */
#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif
#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 23
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 0x0004
#endif
#ifndef NS_ALL
#define NS_ALL 0
#endif
#ifndef NS_DNS
#define NS_DNS 12
#endif
#ifndef DNS_QUERY_NO_WIRE_QUERY
#define DNS_QUERY_NO_WIRE_QUERY 0x00000010
#endif
#ifndef DNS_TYPE_A
#define DNS_TYPE_A 0x0001
#endif
#ifndef DNS_TYPE_AAAA
#define DNS_TYPE_AAAA 0x001C
#endif
#ifndef DNS_TYPE_ANY
#define DNS_TYPE_ANY 0x00FF
#endif

/**
 * @brief Calling convention of the functions of the winsock 2 API.
 *
 * A translation unit which does not include the winsock header does not carry
 * the convention either, so it is declared here as well.
 */
#ifndef WSAAPI
#define WSAAPI __stdcall
#endif
/** @} */

/*
 * The name resolution hints are declared here only while the winsock 2 headers
 * are not part of the translation unit: a loader translation unit includes them
 * through asio, which defines _WINSOCK2API_ and brings the real declarations
 * with it.
 */
#ifndef _WINSOCK2API_

/**
 * @brief Name resolution hints of the winsock 2 API.
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2def/ns-ws2def-addrinfoa
 */
typedef struct _ADDRINFOA
{
    int                ai_flags;
    int                ai_family;
    int                ai_socktype;
    int                ai_protocol;
    SIZE_T             ai_addrlen;
    PSTR               ai_canonname;
    struct sockaddr*   ai_addr;
    struct _ADDRINFOA* ai_next;
} ADDRINFOA, *PADDRINFOA;

/**
 * @brief Name resolution hints of the wide winsock 2 API.
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2def/ns-ws2def-addrinfow
 */
typedef struct _ADDRINFOW
{
    int                ai_flags;
    int                ai_family;
    int                ai_socktype;
    int                ai_protocol;
    SIZE_T             ai_addrlen;
    PWSTR              ai_canonname;
    struct sockaddr*   ai_addr;
    struct _ADDRINFOW* ai_next;
} ADDRINFOW, *PADDRINFOW;

/**
 * @brief Name resolution hints of GetAddrInfoExW().
 *
 * The leading members describe the same request as ADDRINFOW; the trailing ones
 * name the provider of an extended query.
 *
 * @see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/ns-ws2tcpip-addrinfoexw
 */
typedef struct _ADDRINFOEXW
{
    int                  ai_flags;
    int                  ai_family;
    int                  ai_socktype;
    int                  ai_protocol;
    SIZE_T               ai_addrlen;
    PWSTR                ai_canonname;
    struct sockaddr*     ai_addr;
    struct _ADDRINFOEXW* ai_next;
    PVOID                ai_blob;
    SIZE_T               ai_bloblen;
    LPGUID               ai_provider;
} ADDRINFOEXW, *PADDRINFOEXW;

#endif /* _WINSOCK2API_ */

#ifdef __cplusplus
}
#endif
#endif
