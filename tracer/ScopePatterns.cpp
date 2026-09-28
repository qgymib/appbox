#include "tracer/ScopePatterns.hpp"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>

namespace appbox::tracer
{
namespace
{

/** Base name of the module which carries the NT entry points. */
constexpr wchar_t kNtdllModule[] = L"ntdll";

/** Base name of the module which carries the socket API. */
constexpr wchar_t kWs2Module[] = L"ws2_32";

/** Base name of the module which carries the DNS client API. */
constexpr wchar_t kDnsApiModule[] = L"dnsapi";

/*
 * Filesystem entry points of ntdll.
 *
 * The list holds the entry points whose object is a file, a section, a symbolic
 * link, a device or a volume, plus the generic handle close, because a delete
 * on close is committed there. The object manager namespace
 * (`NtOpenDirectoryObject`, `NtCreateDirectoryObject`), the ALPC sections
 * (`NtAlpcCreatePortSection`) and the generic I/O plumbing (`NtCreateIoCompletion`,
 * `NtCreateIoRing`) are not file operations and stay out of the list.
 */
constexpr const wchar_t* kNtFileNames[] = {
    L"NtAreMappedFilesTheSame",
    L"NtCancelIoFile",
    L"NtCancelIoFileEx",
    L"NtCancelSynchronousIoFile",
    L"NtClose",
    L"NtCopyFileChunk",
    L"NtCreateFile",
    L"NtCreatePagingFile",
    L"NtCreateSection",
    L"NtCreateSectionEx",
    L"NtCreateSymbolicLinkObject",
    L"NtDeleteFile",
    L"NtDeviceIoControlFile",
    L"NtExtendSection",
    L"NtFlushBuffersFile",
    L"NtFlushBuffersFileEx",
    L"NtFsControlFile",
    L"NtLockFile",
    L"NtMapViewOfSection",
    L"NtMapViewOfSectionEx",
    L"NtNotifyChangeDirectoryFile",
    L"NtNotifyChangeDirectoryFileEx",
    L"NtOpenFile",
    L"NtOpenSection",
    L"NtOpenSymbolicLinkObject",
    L"NtQueryAttributesFile",
    L"NtQueryDirectoryFile",
    L"NtQueryDirectoryFileEx",
    L"NtQueryEaFile",
    L"NtQueryFullAttributesFile",
    L"NtQueryInformationByName",
    L"NtQueryInformationFile",
    L"NtQueryQuotaInformationFile",
    L"NtQuerySection",
    L"NtQuerySymbolicLinkObject",
    L"NtQueryVolumeInformationFile",
    L"NtReadFile",
    L"NtReadFileScatter",
    L"NtSetEaFile",
    L"NtSetInformationFile",
    L"NtSetInformationSymbolicLink",
    L"NtSetQuotaInformationFile",
    L"NtSetVolumeInformationFile",
    L"NtTranslateFilePath",
    L"NtUnlockFile",
    L"NtUnmapViewOfSection",
    L"NtUnmapViewOfSectionEx",
    L"NtWriteFile",
    L"NtWriteFileGather",
};

/*
 * Registry entry points of ntdll.
 *
 * The list holds every entry point which takes a key as its object, the
 * transaction APIs which operate on a key, the hive level load and save APIs
 * and `NtQueryObject`, which the registry isolation uses to translate the name
 * of a redirected handle back into a view path. The license and MUI helpers
 * (`NtQueryLicenseValue`, `NtGetMUIRegistryInfo`) read registry backed data but
 * do not take a key, so they stay out of the list.
 */
constexpr const wchar_t* kNtRegistryNames[] = {
    L"NtCommitRegistryTransaction",
    L"NtCompactKeys",
    L"NtCompressKey",
    L"NtCreateKey",
    L"NtCreateKeyTransacted",
    L"NtCreateRegistryTransaction",
    L"NtDeleteKey",
    L"NtDeleteValueKey",
    L"NtEnumerateKey",
    L"NtEnumerateValueKey",
    L"NtFlushKey",
    L"NtFreezeRegistry",
    L"NtLoadKey",
    L"NtLoadKey2",
    L"NtLoadKey3",
    L"NtLoadKeyEx",
    L"NtLockRegistryKey",
    L"NtNotifyChangeKey",
    L"NtNotifyChangeMultipleKeys",
    L"NtOpenKey",
    L"NtOpenKeyEx",
    L"NtOpenKeyTransacted",
    L"NtOpenKeyTransactedEx",
    L"NtOpenRegistryTransaction",
    L"NtQueryKey",
    L"NtQueryMultipleValueKey",
    L"NtQueryObject",
    L"NtQueryOpenSubKeys",
    L"NtQueryOpenSubKeysEx",
    L"NtQueryValueKey",
    L"NtRenameKey",
    L"NtReplaceKey",
    L"NtRestoreKey",
    L"NtRollbackRegistryTransaction",
    L"NtSaveKey",
    L"NtSaveKeyEx",
    L"NtSaveMergedKeys",
    L"NtSetInformationKey",
    L"NtSetValueKey",
    L"NtThawRegistry",
    L"NtUnloadKey",
    L"NtUnloadKey2",
    L"NtUnloadKeyEx",
};

/*
 * Network entry points of ntdll.
 *
 * Name resolution has no NT entry point at all: a DNS query leaves the process
 * through the socket library, which is why the network domain is the only one
 * that reaches beyond ntdll (see the two lists below). What ntdll itself
 * contributes is the named pipe and mailslot creation and the device control
 * path which carries the socket requests of the ancillary function driver.
 */
constexpr const wchar_t* kNtNetworkNames[] = {
    L"NtCreateMailslotFile",
    L"NtCreateNamedPipeFile",
    L"NtDeviceIoControlFile",
    L"NtFsControlFile",
};

/*
 * Name resolution entry points of the socket library.
 *
 * These are the lowest landing points a name resolution can have, because the
 * query is issued by user mode code: `getaddrinfo` and its wide and extended
 * variants, the legacy host lookups, the reverse lookup, the service lookup of
 * the namespace provider and the asynchronous variants. The helpers which only
 * free or configure a result (`FreeAddrInfoW`, `SetAddrInfoExW`), the namespace
 * provider administration (`WSC*`) and the address formatting APIs are not
 * lookups and stay out of the list.
 */
constexpr const wchar_t* kWs2NameResolutionNames[] = {
    L"GetAddrInfoExA",         L"GetAddrInfoExW",        L"GetAddrInfoW",          L"GetHostNameW",
    L"GetNameInfoW",           L"WSAAsyncGetHostByAddr", L"WSAAsyncGetHostByName", L"WSALookupServiceBeginA",
    L"WSALookupServiceBeginW", L"WSALookupServiceEnd",   L"WSALookupServiceNextA", L"WSALookupServiceNextW",
    L"gethostbyaddr",          L"gethostbyname",         L"gethostname",           L"getaddrinfo",
};

/*
 * Name resolution entry points of the DNS client.
 *
 * The resolver API of the DNS client is what `getaddrinfo` ends up in for a
 * regular query: the synchronous, the extended and the asynchronous query, the
 * cancel path of the asynchronous one and the service resolution of the
 * multicast DNS client. The record and message helpers (`DnsExtractRecordsFromMessage_UTF8`,
 * `DnsWriteQuestionToBuffer_UTF8`), the name validation (`DnsValidateName_W`)
 * and the resolver configuration (`DnsQueryConfig`) are not lookups and stay
 * out of the list.
 */
constexpr const wchar_t* kDnsApiNameResolutionNames[] = {
    L"DnsCancelQuery", L"DnsQueryEx",    L"DnsQueryExA", L"DnsQueryExUTF8",    L"DnsQueryExW",
    L"DnsQuery_A",     L"DnsQuery_UTF8", L"DnsQuery_W",  L"DnsServiceResolve", L"DnsServiceResolveCancel",
};

/** One name list together with the module which exports it and its category. */
struct ScopeGroup
{
    const wchar_t*        module;   ///< Module which exports the names.
    Category              category; ///< Category every name of the list belongs to.
    const wchar_t* const* names;    ///< Exported names.
    std::size_t           count;    ///< Number of names.
};

/** The scope, grouped per domain and per module. */
constexpr ScopeGroup kScopeGroups[] = {
    { kNtdllModule,  Category::File,     kNtFileNames,               std::size(kNtFileNames)               },
    { kNtdllModule,  Category::Registry, kNtRegistryNames,           std::size(kNtRegistryNames)           },
    { kNtdllModule,  Category::Network,  kNtNetworkNames,            std::size(kNtNetworkNames)            },
    { kWs2Module,    Category::Network,  kWs2NameResolutionNames,    std::size(kWs2NameResolutionNames)    },
    { kDnsApiModule, Category::Network,  kDnsApiNameResolutionNames, std::size(kDnsApiNameResolutionNames) },
};

/** Categories of one name. */
using CategoryList = std::vector<Category>;

/** Lookup index: module, then exported name, then the categories of that name. */
using CategoryIndex = std::map<std::wstring, std::map<std::wstring, CategoryList>>;

/**
 * @brief Add a category to a list without duplicating it.
 *
 * @param[in,out] categories Category list to extend.
 * @param[in] category Category to add.
 */
void AddCategory(CategoryList& categories, Category category)
{
    if (std::find(categories.begin(), categories.end(), category) == categories.end())
    {
        categories.push_back(category);
    }
}

/**
 * @brief Build the lookup index from the scope table.
 *
 * @return The index, keyed by module and exported name.
 */
CategoryIndex BuildIndex()
{
    CategoryIndex index;
    for (const auto& entry : ScopeTable())
    {
        AddCategory(index[entry.module][entry.name], entry.category);
    }

    return index;
}

/**
 * @brief The lookup index, built on first use.
 *
 * @return The index; it is immutable after it was built, so it is shared by all
 *         callers.
 */
const CategoryIndex& Index()
{
    static const CategoryIndex index = BuildIndex();
    return index;
}

/**
 * @brief Report whether a name is the `Zw` spelling of an NT entry point.
 *
 * @param[in] name Export name to test.
 * @return Whether the name is `Zw` followed by an uppercase letter.
 */
bool IsZwAlias(const std::wstring& name)
{
    if (name.size() <= 2U || name.compare(0, 2U, L"Zw") != 0)
    {
        return false;
    }

    const wchar_t next = name[2];
    return next >= L'A' && next <= L'Z';
}

} // namespace

std::vector<ScopeEntry> ScopeTable()
{
    std::vector<ScopeEntry> entries;
    for (const auto& group : kScopeGroups)
    {
        for (std::size_t index = 0; index < group.count; ++index)
        {
            entries.push_back(ScopeEntry{ group.module, group.category, group.names[index] });
        }
    }

    return entries;
}

std::vector<Category> ClassifyExport(const std::wstring& module, const std::wstring& name)
{
    if (module.empty() || name.empty())
    {
        return {};
    }

    const CategoryIndex& index = Index();
    const auto           module_entry = index.find(module);
    if (module_entry == index.end())
    {
        return {};
    }

    const auto found = module_entry->second.find(name);
    if (found != module_entry->second.end())
    {
        return found->second;
    }

    /* The `Zw` alias shares the address and the categories of its `Nt` name. */
    if (module == kNtdllModule && IsZwAlias(name))
    {
        const auto alias = module_entry->second.find(L"Nt" + name.substr(2U));
        if (alias != module_entry->second.end())
        {
            return alias->second;
        }
    }

    return {};
}

bool MatchesCategory(const std::wstring& module, const std::wstring& name, Category category)
{
    const std::vector<Category> categories = ClassifyExport(module, name);
    return std::find(categories.begin(), categories.end(), category) != categories.end();
}

} // namespace appbox::tracer
