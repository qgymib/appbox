#include <gtest/gtest.h>
#include "tracer/ScopePatterns.hpp"
#include "tracer/TracedModules.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

/** Category values, aliased for readability. */
constexpr auto kFile = appbox::tracer::Category::File;
constexpr auto kRegistry = appbox::tracer::Category::Registry;
constexpr auto kNetwork = appbox::tracer::Category::Network;

/** Machine type of an x64 image (IMAGE_FILE_MACHINE_AMD64). */
constexpr std::uint16_t kMachineAmd64 = 0x8664;

/**
 * @brief Report whether every name of a list belongs to a category.
 *
 * @param[in] module Module which exports the names.
 * @param[in] names Export names to test.
 * @param[in] category Category the names have to belong to.
 */
void ExpectInCategory(const wchar_t* module, const std::vector<const wchar_t*>& names,
                      appbox::tracer::Category category)
{
    for (const auto* name : names)
    {
        EXPECT_TRUE(appbox::tracer::MatchesCategory(module, name, category)) << module << L"!" << name;
    }
}

/**
 * @brief Report whether every name of a list is out of the scope.
 *
 * @param[in] module Module which exports the names.
 * @param[in] names Export names to test.
 */
void ExpectOutOfScope(const wchar_t* module, const std::vector<const wchar_t*>& names)
{
    for (const auto* name : names)
    {
        EXPECT_TRUE(appbox::tracer::ClassifyExport(module, name).empty()) << module << L"!" << name;
    }
}

} // namespace

/**
 * @brief The filesystem entry points of ntdll are part of the filesystem scope,
 *        including the ones which a keyword match never reached (the symbolic
 *        link, the read and write and the section APIs).
 */
TEST(Unit_TracerScope, FilesystemEntryPointsAreInTheScope)
{
    ExpectInCategory(L"ntdll",
                     { L"NtCreateFile",
                       L"NtOpenFile",
                       L"NtDeleteFile",
                       L"NtCopyFileChunk",
                       L"NtCreatePagingFile",
                       L"NtTranslateFilePath",
                       L"NtQueryAttributesFile",
                       L"NtQueryFullAttributesFile",
                       L"NtQueryInformationByName",
                       L"NtQueryDirectoryFile",
                       L"NtQueryDirectoryFileEx",
                       L"NtNotifyChangeDirectoryFile",
                       L"NtNotifyChangeDirectoryFileEx",
                       L"NtReadFile",
                       L"NtReadFileScatter",
                       L"NtWriteFile",
                       L"NtWriteFileGather",
                       L"NtQueryInformationFile",
                       L"NtSetInformationFile",
                       L"NtQueryEaFile",
                       L"NtSetEaFile",
                       L"NtQueryVolumeInformationFile",
                       L"NtSetVolumeInformationFile",
                       L"NtQueryQuotaInformationFile",
                       L"NtSetQuotaInformationFile",
                       L"NtLockFile",
                       L"NtUnlockFile",
                       L"NtFlushBuffersFile",
                       L"NtFlushBuffersFileEx",
                       L"NtCancelIoFile",
                       L"NtCancelIoFileEx",
                       L"NtCancelSynchronousIoFile",
                       L"NtDeviceIoControlFile",
                       L"NtFsControlFile",
                       L"NtCreateSection",
                       L"NtCreateSectionEx",
                       L"NtOpenSection",
                       L"NtQuerySection",
                       L"NtExtendSection",
                       L"NtMapViewOfSection",
                       L"NtMapViewOfSectionEx",
                       L"NtUnmapViewOfSection",
                       L"NtUnmapViewOfSectionEx",
                       L"NtAreMappedFilesTheSame",
                       L"NtCreateSymbolicLinkObject",
                       L"NtOpenSymbolicLinkObject",
                       L"NtQuerySymbolicLinkObject",
                       L"NtSetInformationSymbolicLink",
                       L"NtClose" },
                     kFile);
}

/**
 * @brief The registry entry points of ntdll are part of the registry scope,
 *        including the hive APIs, the transactions and the name translation the
 *        registry isolation needs.
 */
TEST(Unit_TracerScope, RegistryEntryPointsAreInTheScope)
{
    ExpectInCategory(L"ntdll",
                     { L"NtOpenKey",
                       L"NtOpenKeyEx",
                       L"NtOpenKeyTransacted",
                       L"NtOpenKeyTransactedEx",
                       L"NtCreateKey",
                       L"NtCreateKeyTransacted",
                       L"NtDeleteKey",
                       L"NtRenameKey",
                       L"NtQueryKey",
                       L"NtSetInformationKey",
                       L"NtEnumerateKey",
                       L"NtEnumerateValueKey",
                       L"NtQueryValueKey",
                       L"NtQueryMultipleValueKey",
                       L"NtSetValueKey",
                       L"NtDeleteValueKey",
                       L"NtQueryOpenSubKeys",
                       L"NtQueryOpenSubKeysEx",
                       L"NtNotifyChangeKey",
                       L"NtNotifyChangeMultipleKeys",
                       L"NtFlushKey",
                       L"NtLoadKey",
                       L"NtLoadKey2",
                       L"NtLoadKey3",
                       L"NtLoadKeyEx",
                       L"NtUnloadKey",
                       L"NtUnloadKey2",
                       L"NtUnloadKeyEx",
                       L"NtSaveKey",
                       L"NtSaveKeyEx",
                       L"NtSaveMergedKeys",
                       L"NtRestoreKey",
                       L"NtReplaceKey",
                       L"NtCompactKeys",
                       L"NtCompressKey",
                       L"NtLockRegistryKey",
                       L"NtFreezeRegistry",
                       L"NtThawRegistry",
                       L"NtCreateRegistryTransaction",
                       L"NtOpenRegistryTransaction",
                       L"NtCommitRegistryTransaction",
                       L"NtRollbackRegistryTransaction",
                       L"NtQueryObject" },
                     kRegistry);
}

/**
 * @brief The network scope holds the named pipe and mailslot entry points of
 *        ntdll together with the device control path the socket requests use.
 */
TEST(Unit_TracerScope, NetworkEntryPointsAreInTheScope)
{
    ExpectInCategory(
        L"ntdll", { L"NtCreateNamedPipeFile", L"NtCreateMailslotFile", L"NtDeviceIoControlFile", L"NtFsControlFile" },
        kNetwork);
}

/**
 * @brief Name resolution has no NT landing point, so the socket library and the
 *        DNS client are part of the network scope: their resolution entry points
 *        are the lowest ones a lookup can have.
 */
TEST(Unit_TracerScope, NameResolutionIsPartOfTheNetworkScope)
{
    ExpectInCategory(L"ws2_32",
                     { L"getaddrinfo", L"GetAddrInfoW", L"GetAddrInfoExW", L"GetAddrInfoExA", L"gethostbyname",
                       L"gethostbyaddr", L"gethostname", L"GetHostNameW", L"GetNameInfoW", L"WSAAsyncGetHostByName",
                       L"WSAAsyncGetHostByAddr", L"WSALookupServiceBeginA", L"WSALookupServiceBeginW",
                       L"WSALookupServiceNextA", L"WSALookupServiceNextW", L"WSALookupServiceEnd" },
                     kNetwork);

    ExpectInCategory(L"dnsapi",
                     { L"DnsQuery_A", L"DnsQuery_W", L"DnsQuery_UTF8", L"DnsQueryEx", L"DnsQueryExA", L"DnsQueryExW",
                       L"DnsQueryExUTF8", L"DnsCancelQuery", L"DnsServiceResolve", L"DnsServiceResolveCancel" },
                     kNetwork);
}

/**
 * @brief The default scope is a superset of the name resolution entry points the
 *        sandbox hooks, which is what makes the tracer usable for the DNS
 *        isolation; the sandbox hooks nothing else, and the scope does not
 *        depend on that.
 */
TEST(Unit_TracerScope, TheNetworkScopeCoversTheHookedEntryPoints)
{
    ExpectInCategory(L"ws2_32", { L"GetAddrInfoW", L"getaddrinfo", L"GetAddrInfoExW", L"gethostbyname" }, kNetwork);
    ExpectInCategory(L"dnsapi", { L"DnsQuery_A", L"DnsQuery_W", L"DnsQuery_UTF8" }, kNetwork);

    /* Entry points the sandbox does not hook are part of the domain as well. */
    ExpectInCategory(L"ntdll", { L"NtSetInformationFile", L"NtQueryEaFile" }, kFile);
    ExpectInCategory(L"ntdll", { L"NtSaveKey", L"NtReplaceKey" }, kRegistry);
    ExpectInCategory(L"ws2_32", { L"GetNameInfoW", L"gethostbyaddr" }, kNetwork);
}

/**
 * @brief A function which works on files and on sockets belongs to both
 *        categories, because either isolation domain has to know about it.
 */
TEST(Unit_TracerScope, DeviceControlIsAFileAndNetworkFunction)
{
    const auto categories = appbox::tracer::ClassifyExport(L"ntdll", L"NtDeviceIoControlFile");
    ASSERT_EQ(categories.size(), 2U);
    EXPECT_TRUE(appbox::tracer::MatchesCategory(L"ntdll", L"NtDeviceIoControlFile", kFile));
    EXPECT_TRUE(appbox::tracer::MatchesCategory(L"ntdll", L"NtDeviceIoControlFile", kNetwork));

    EXPECT_TRUE(appbox::tracer::MatchesCategory(L"ntdll", L"NtFsControlFile", kFile));
    EXPECT_TRUE(appbox::tracer::MatchesCategory(L"ntdll", L"NtFsControlFile", kNetwork));
}

/**
 * @brief The Zw aliases of an NT entry point share its address, so they have to
 *        share its categories as well.
 */
TEST(Unit_TracerScope, ZwAliasesHaveTheSameCategoriesAsTheirNtCounterparts)
{
    EXPECT_EQ(appbox::tracer::ClassifyExport(L"ntdll", L"ZwCreateFile"),
              appbox::tracer::ClassifyExport(L"ntdll", L"NtCreateFile"));
    EXPECT_EQ(appbox::tracer::ClassifyExport(L"ntdll", L"ZwOpenKey"),
              appbox::tracer::ClassifyExport(L"ntdll", L"NtOpenKey"));
    EXPECT_EQ(appbox::tracer::ClassifyExport(L"ntdll", L"ZwDeviceIoControlFile"),
              appbox::tracer::ClassifyExport(L"ntdll", L"NtDeviceIoControlFile"));
    EXPECT_EQ(appbox::tracer::ClassifyExport(L"ntdll", L"ZwReadFile"),
              appbox::tracer::ClassifyExport(L"ntdll", L"NtReadFile"));
}

/**
 * @brief The Win32 wrappers and the path helpers of ntdll are not part of the
 *        default scope: the scope is the lowest level of a domain, and the
 *        wrappers are only reachable through `--all-exports`.
 */
TEST(Unit_TracerScope, Win32WrappersAndPathHelpersAreOutOfTheScope)
{
    ExpectOutOfScope(L"kernel32",
                     { L"CreateFileW", L"CreateFile2", L"ReadFile", L"WriteFile", L"DeleteFileW", L"FindFirstFileExW",
                       L"GetFileAttributesExW", L"SetEndOfFile", L"DeviceIoControl", L"CreateFileMappingW",
                       L"MapViewOfFile", L"CreateNamedPipeW", L"ConnectNamedPipe", L"PeekNamedPipe", L"CreateMailslotW",
                       L"GetFullPathNameW", L"GetTempPath2W", L"QueryDosDeviceW" });

    ExpectOutOfScope(L"kernelbase",
                     { L"CreateFileW", L"ReadFile", L"DeleteFileW", L"RegOpenKeyExW", L"RegQueryValueExW",
                       L"RegCloseKey", L"DeviceIoControl", L"GetFinalPathNameByHandleW" });

    ExpectOutOfScope(L"ntdll", { L"RtlDosPathNameToNtPathName_U", L"RtlGetFullPathName_U", L"RtlIsDosDeviceName_U",
                                 L"RtlQueryRegistryValues", L"RtlCreateRegistryKey" });

    /* Helpers of the name resolution are not lookups either. */
    ExpectOutOfScope(L"ws2_32", { L"FreeAddrInfoW", L"FreeAddrInfoExW", L"SetAddrInfoExW", L"GetAddrInfoExCancel",
                                  L"WSAAddressToStringW", L"WSCInstallNameSpace" });
    ExpectOutOfScope(L"dnsapi", { L"DnsValidateName_W", L"DnsQueryConfig", L"DnsFree", L"DnsRecordListFree",
                                  L"DnsExtractRecordsFromMessage_UTF8" });
}

/**
 * @brief The module a name comes from is part of the decision: an NT entry point
 *        is only classified for ntdll, a name resolution entry point only for
 *        the module which implements it.
 */
TEST(Unit_TracerScope, TheModuleIsPartOfTheDecision)
{
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"kernel32", L"NtCreateFile").empty());
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"kernelbase", L"NtOpenKey").empty());
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"ntdll", L"GetAddrInfoW").empty());
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"ws2_32", L"DnsQuery_W").empty());
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"dnsapi", L"GetAddrInfoW").empty());
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"unknown", L"NtCreateFile").empty());
}

/**
 * @brief Names which only look related must stay out of the scope. Every name
 *        of this list is a false positive a keyword match would produce: an
 *        ALPC section, an object manager directory, a keyed event, a storage
 *        partition, an I/O completion port or an I/O ring.
 */
TEST(Unit_TracerScope, UnrelatedNamesAreNotInTheScope)
{
    ExpectOutOfScope(L"ntdll", { L"NtCreateDirectoryObject",  L"NtCreateDirectoryObjectEx",
                                 L"NtOpenDirectoryObject",    L"NtQueryDirectoryObject",
                                 L"NtAlpcCreatePortSection",  L"NtAlpcCreateSectionView",
                                 L"NtAlpcDeletePortSection",  L"NtCreateKeyedEvent",
                                 L"NtOpenKeyedEvent",         L"NtWaitForKeyedEvent",
                                 L"NtReleaseKeyedEvent",      L"NtCreateIoCompletion",
                                 L"NtCreateIoRing",           L"NtSubmitIoRing",
                                 L"NtCreatePartition",        L"NtOpenPartition",
                                 L"NtManagePartition",        L"NtCreateCpuPartition",
                                 L"NtGetNlsSectionPtr",       L"NtMapCMFModule",
                                 L"NtQueryLicenseValue",      L"NtGetMUIRegistryInfo",
                                 L"NtFlushWriteBuffer",       L"NtAllocateVirtualMemory",
                                 L"NtQuerySystemInformation", L"NtWaitForSingleObject",
                                 L"RtlAllocateHeap" });

    ExpectOutOfScope(L"kernel32", { L"EtwEventRegister", L"RegisterWaitForSingleObject", L"GetTickCount64" });
    ExpectOutOfScope(L"ntdll", { L"DbgUiConnectToDbg", L"LdrGetProcedureAddress" });
}

/**
 * @brief Every name of the scope table is an executable export of the module the
 *        table assigns it to. This is what keeps the table honest: a name which
 *        does not exist or which is a forwarder can never be armed.
 */
TEST(Unit_TracerScope, EveryScopeEntryIsAnExecutableExportOfItsModule)
{
    const auto modules = appbox::tracer::LoadTracedModules(appbox::tracer::SystemDirectoryForMachine(kMachineAmd64));
    ASSERT_FALSE(modules.empty());

    for (const auto& entry : appbox::tracer::ScopeTable())
    {
        const auto module = modules.find(entry.module);
        ASSERT_NE(module, modules.end()) << entry.module;

        const auto& exports = module->second.image.Exports();
        const auto  found =
            std::find_if(exports.begin(), exports.end(), [&entry](const appbox::tracer::ExportEntry& candidate) {
                return candidate.name == entry.name;
            });
        ASSERT_NE(found, exports.end()) << entry.module << L"!" << entry.name;
        EXPECT_TRUE(found->forwarder.empty()) << entry.module << L"!" << entry.name;
        EXPECT_TRUE(module->second.image.IsExecutable(found->rva)) << entry.module << L"!" << entry.name;
    }
}

/**
 * @brief The table lists a name once per category, and it covers a domain with
 *        more than a handful of names, so a table which lost entries is noticed.
 */
TEST(Unit_TracerScope, TheScopeTableIsWellFormed)
{
    std::map<std::pair<std::wstring, std::wstring>, std::set<appbox::tracer::Category>> categories;
    std::map<appbox::tracer::Category, std::size_t>                                     counts;

    for (const auto& entry : appbox::tracer::ScopeTable())
    {
        const bool inserted = categories[{ entry.module, entry.name }].insert(entry.category).second;
        EXPECT_TRUE(inserted) << entry.module << L"!" << entry.name;
        ++counts[entry.category];
    }

    EXPECT_GE(counts[kFile], 40U);
    EXPECT_GE(counts[kRegistry], 35U);
    EXPECT_GE(counts[kNetwork], 25U);
}

/**
 * @brief A name without any relation to the three domains has no category, and
 *        an empty module or name is handled without a special case.
 */
TEST(Unit_TracerScope, NamesWithoutARelationHaveNoCategory)
{
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"", L"").empty());
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"", L"NtCreateFile").empty());
    EXPECT_TRUE(appbox::tracer::ClassifyExport(L"ntdll", L"").empty());
    EXPECT_FALSE(appbox::tracer::MatchesCategory(L"ntdll", L"NtQuerySystemInformation", kRegistry));
    EXPECT_FALSE(appbox::tracer::MatchesCategory(L"ntdll", L"ZwQuerySystemInformation", kRegistry));
}
