#include <gtest/gtest.h>
#include "tracer/ScopePatterns.hpp"
#include <string>
#include <vector>

namespace
{

/** Category values, aliased for readability. */
constexpr auto kFile = appbox::tracer::Category::File;
constexpr auto kRegistry = appbox::tracer::Category::Registry;
constexpr auto kNetwork = appbox::tracer::Category::Network;

/**
 * @brief Report whether every name of a list belongs to a category.
 *
 * @param[in] names Export names to test.
 * @param[in] category Category the names have to belong to.
 */
void ExpectInCategory(const std::vector<const wchar_t*>& names, appbox::tracer::Category category)
{
    for (const auto* name : names)
    {
        EXPECT_TRUE(appbox::tracer::MatchesCategory(name, category)) << name;
    }
}

} // namespace

/**
 * @brief The functions the filesystem isolation documents as its entry points
 *        are part of the filesystem scope.
 */
TEST(TracerScope, FilesystemEntryPointsAreInTheScope)
{
    ExpectInCategory({L"NtCreateFile",
                      L"NtOpenFile",
                      L"NtQueryAttributesFile",
                      L"NtQueryFullAttributesFile",
                      L"NtQueryInformationByName",
                      L"NtQueryDirectoryFile",
                      L"NtQueryDirectoryFileEx",
                      L"NtDeleteFile",
                      L"NtQueryInformationFile",
                      L"NtSetInformationFile",
                      L"NtQueryVolumeInformationFile",
                      L"NtDeviceIoControlFile",
                      L"NtFsControlFile",
                      L"NtClose",
                      L"NtCreateSection",
                      L"NtMapViewOfSection",
                      L"NtNotifyChangeDirectoryFile"},
                     kFile);
}

/**
 * @brief The Win32 wrappers of the filesystem entry points are part of the
 *        scope as well: an application usually calls them, not the NT API.
 */
TEST(TracerScope, FilesystemWrappersAreInTheScope)
{
    ExpectInCategory({L"CreateFileW",
                      L"CreateFile2",
                      L"ReadFile",
                      L"ReadFileEx",
                      L"WriteFile",
                      L"DeleteFileW",
                      L"CopyFile2",
                      L"MoveFileExW",
                      L"ReplaceFileW",
                      L"CreateDirectoryW",
                      L"RemoveDirectoryW",
                      L"FindFirstFileExW",
                      L"FindNextFileW",
                      L"FindClose",
                      L"GetFileAttributesExW",
                      L"SetFileAttributesW",
                      L"GetFileSizeEx",
                      L"SetFilePointerEx",
                      L"SetEndOfFile",
                      L"FlushFileBuffers",
                      L"LockFileEx",
                      L"GetFullPathNameW",
                      L"GetTempPath2W",
                      L"GetTempFileNameW",
                      L"GetCurrentDirectoryW",
                      L"SetCurrentDirectoryW",
                      L"GetDiskFreeSpaceExW",
                      L"GetLogicalDriveStringsW",
                      L"GetDriveTypeW",
                      L"GetVolumeInformationByHandleW",
                      L"QueryDosDeviceW",
                      L"DefineDosDeviceW",
                      L"CreateSymbolicLinkW",
                      L"CreateHardLinkW",
                      L"GetFinalPathNameByHandleW",
                      L"ReadDirectoryChangesW",
                      L"CreateFileMappingW",
                      L"MapViewOfFile",
                      L"UnmapViewOfFile",
                      L"OpenFileMappingW",
                      L"GetOverlappedResult",
                      L"DeviceIoControl",
                      L"RtlDosPathNameToNtPathName_U",
                      L"RtlGetFullPathName_U"},
                     kFile);
}

/**
 * @brief The registry entry points of the registry isolation are in the scope,
 *        including the value level APIs it deliberately does not hook.
 */
TEST(TracerScope, RegistryEntryPointsAreInTheScope)
{
    ExpectInCategory({L"NtOpenKey",
                      L"NtOpenKeyEx",
                      L"NtCreateKey",
                      L"NtCreateKeyTransacted",
                      L"NtDeleteKey",
                      L"NtQueryKey",
                      L"NtEnumerateKey",
                      L"NtEnumerateValueKey",
                      L"NtQueryValueKey",
                      L"NtSetValueKey",
                      L"NtDeleteValueKey",
                      L"NtQueryMultipleValueKey",
                      L"NtSaveKey",
                      L"NtLoadKey",
                      L"NtUnloadKey",
                      L"NtRestoreKey",
                      L"NtReplaceKey",
                      L"NtNotifyChangeKey",
                      L"NtFlushKey",
                      L"NtRenameKey",
                      L"NtCompactKeys",
                      L"NtQueryObject",
                      L"RegOpenKeyExW",
                      L"RegCreateKeyExW",
                      L"RegQueryValueExW",
                      L"RegSetValueExW",
                      L"RegDeleteKeyW",
                      L"RegDeleteValueW",
                      L"RegEnumKeyExW",
                      L"RegEnumValueW",
                      L"RegCloseKey",
                      L"RegGetValueW"},
                     kRegistry);
}

/**
 * @brief The network scope covers the named pipe, mailslot and device control
 *        entry points, which is how network I/O reaches the kernel through the
 *        three traced DLLs.
 */
TEST(TracerScope, NetworkEntryPointsAreInTheScope)
{
    ExpectInCategory({L"NtCreateNamedPipeFile",
                      L"NtCreateMailslotFile",
                      L"NtDeviceIoControlFile",
                      L"NtFsControlFile",
                      L"CreateNamedPipeW",
                      L"ConnectNamedPipe",
                      L"DisconnectNamedPipe",
                      L"CallNamedPipeW",
                      L"TransactNamedPipe",
                      L"PeekNamedPipe",
                      L"WaitNamedPipeW",
                      L"GetNamedPipeInfo",
                      L"SetNamedPipeHandleState",
                      L"ImpersonateNamedPipeClient",
                      L"CreateMailslotW",
                      L"GetMailslotInfo",
                      L"SetMailslotInfo"},
                     kNetwork);
}

/**
 * @brief A function which works on files and on sockets belongs to both
 *        categories, because either isolation domain has to know about it.
 */
TEST(TracerScope, DeviceControlIsAFileAndNetworkFunction)
{
    const auto categories = appbox::tracer::ClassifyExport(L"NtDeviceIoControlFile");
    ASSERT_EQ(categories.size(), 2U);
    EXPECT_TRUE(appbox::tracer::MatchesCategory(L"NtDeviceIoControlFile", kFile));
    EXPECT_TRUE(appbox::tracer::MatchesCategory(L"NtDeviceIoControlFile", kNetwork));

    EXPECT_TRUE(appbox::tracer::MatchesCategory(L"NtFsControlFile", kFile));
    EXPECT_TRUE(appbox::tracer::MatchesCategory(L"NtFsControlFile", kNetwork));
}

/**
 * @brief The Zw aliases of an NT entry point share its address, so they have to
 *        share its categories as well.
 */
TEST(TracerScope, ZwAliasesHaveTheSameCategoriesAsTheirNtCounterparts)
{
    EXPECT_EQ(appbox::tracer::ClassifyExport(L"ZwCreateFile"),
              appbox::tracer::ClassifyExport(L"NtCreateFile"));
    EXPECT_EQ(appbox::tracer::ClassifyExport(L"ZwOpenKey"),
              appbox::tracer::ClassifyExport(L"NtOpenKey"));
    EXPECT_EQ(appbox::tracer::ClassifyExport(L"ZwDeviceIoControlFile"),
              appbox::tracer::ClassifyExport(L"NtDeviceIoControlFile"));
}

/**
 * @brief Names which only look related must stay out of the scope. Every name
 *        of this list is a false positive a plain substring match produced when
 *        the patterns were measured against the three system DLLs.
 */
TEST(TracerScope, UnrelatedNamesAreNotInTheScope)
{
    const std::vector<const wchar_t*> names = {
        L"EtwEventRegister",           /* `reg` inside "Register" */
        L"EtwNotificationRegister",
        L"EtwEventUnregister",
        L"AlpcRegisterCompletionList",
        L"CsrVerifyRegion",            /* `reg` inside "Region" */
        L"DbgUiConnectToDbg",          /* `connect` inside "ConnectToDbg" */
        L"EtwSendNotification",        /* `send` inside "Send" */
        L"LdrGetProcedureAddress",     /* `addr` inside "Address" */
        L"LdrAddDllDirectory",         /* `directory` inside "DllDirectory" */
        L"LdrGetDllDirectory",
        L"LdrGetFileNameFromLoadAsDataTable",
        L"NtCreateDirectoryObject",    /* object manager directory */
        L"NtOpenDirectoryObject",
        L"NtAlpcCreatePortSection",    /* ALPC section, not a file section */
        L"NtAlpcCreateSectionView",
        L"NtCreateKeyedEvent",         /* synchronization object, not a registry key */
        L"NtWaitForKeyedEvent",
        L"NtReleaseKeyedEvent",
        L"RegisterWaitForSingleObject",
        L"RtlAllocateHeap",
        L"NtAllocateVirtualMemory",
        L"GetTickCount64",
    };

    for (const auto* name : names)
    {
        EXPECT_TRUE(appbox::tracer::ClassifyExport(name).empty()) << name;
    }
}

/**
 * @brief A name without any relation to the three domains has no category, and
 *        an empty name is handled without a special case.
 */
TEST(TracerScope, NamesWithoutARelationHaveNoCategory)
{
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"").empty());
    EXPECT_FALSE(appbox::tracer::MatchesCategory(L"", kFile));
    EXPECT_FALSE(appbox::tracer::MatchesCategory(L"NtQuerySystemInformation", kRegistry));
}
