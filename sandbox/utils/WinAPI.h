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

typedef struct _FILE_STANDARD_INFORMATION
{
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG         NumberOfLinks;
    BOOLEAN       DeletePending;
    BOOLEAN       Directory;
} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

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

#ifdef __cplusplus
}
#endif
#endif
