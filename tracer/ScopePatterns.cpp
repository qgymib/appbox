#include "tracer/ScopePatterns.hpp"
#include <algorithm>
#include <cwchar>

namespace appbox::tracer
{
namespace
{

/** How a name pattern is compared with an export name. */
enum class MatchKind
{
    Exact,       ///< The name equals the pattern.
    Prefix,      ///< The name starts with the pattern.
    PrefixUpper, ///< The name starts with the pattern and continues with an uppercase letter.
    Contains,    ///< The name contains the pattern.
};

/** Rule which matches a name of any shape. */
struct NameRule
{
    Category category;      ///< Category the rule assigns.
    MatchKind kind;         ///< How the pattern is compared.
    const wchar_t* pattern; ///< Fragment to match.
};

/** Rule which matches a keyword inside an Nt/Zw entry point. */
struct NtKeywordRule
{
    Category category;      ///< Category the rule assigns.
    const wchar_t* keyword; ///< Fragment which has to occur in the name.
};

/*
 * Fragments which disqualify a name from every category. Each one is a false
 * positive a plain substring match produces on the three system DLLs:
 *  - `Alpc...Section` / `Alpc...View` are ALPC objects, not file sections;
 *  - `...DirectoryObject` is an object manager directory, not a file directory;
 *  - `...KeyedEvent` is a synchronization object whose name contains `Key`.
 */
const wchar_t* const kExcludedFragments[] = {
    L"Alpc",
    L"DirectoryObject",
    L"KeyedEvent",
};

/*
 * Keywords which make an Nt/Zw entry point part of a category. The keywords are
 * the object or operation the entry point works on, which is exactly what the
 * isolation domains are about.
 */
constexpr NtKeywordRule kNtKeywordRules[] = {
    {Category::File, L"File"},
    {Category::File, L"Directory"},
    {Category::File, L"Volume"},
    {Category::File, L"Section"},
    {Category::File, L"Symlink"},
    {Category::File, L"Reparse"},
    {Category::File, L"Stream"},
    {Category::File, L"Mapped"},
    {Category::File, L"Mapping"},
    {Category::Registry, L"Key"},
    {Category::Registry, L"ValueKey"},
    {Category::Network, L"NamedPipe"},
    {Category::Network, L"Mailslot"},
    {Category::Network, L"DeviceIoControl"},
    {Category::Network, L"FsControl"},
};

/*
 * Rules for names the keyword rules can not express: the documented hooks whose
 * name carries no keyword, the path helpers of ntdll and the Win32 wrappers of
 * the NT entry points.
 */
constexpr NameRule kNameRules[] = {
    /* Filesystem entry points without a keyword in their name. The filesystem
     * isolation hooks NtClose for delete-on-close, which is why the generic
     * handle close is part of the filesystem scope. */
    {Category::File, MatchKind::Exact, L"NtClose"},
    {Category::File, MatchKind::Exact, L"ZwClose"},
    {Category::File, MatchKind::Exact, L"NtQueryInformationByName"},
    {Category::File, MatchKind::Exact, L"ZwQueryInformationByName"},
    {Category::File, MatchKind::Contains, L"RtlDosPathName"},
    {Category::File, MatchKind::Contains, L"RtlGetFullPathName"},
    {Category::File, MatchKind::Contains, L"RtlIsDosDeviceName"},

    /* Win32 wrappers of the filesystem entry points. */
    {Category::File, MatchKind::Prefix, L"CreateFile"},
    {Category::File, MatchKind::Prefix, L"ReadFile"},
    {Category::File, MatchKind::Prefix, L"WriteFile"},
    {Category::File, MatchKind::Prefix, L"DeleteFile"},
    {Category::File, MatchKind::Prefix, L"CopyFile"},
    {Category::File, MatchKind::Prefix, L"MoveFile"},
    {Category::File, MatchKind::Prefix, L"ReplaceFile"},
    {Category::File, MatchKind::Prefix, L"CreateDirectory"},
    {Category::File, MatchKind::Prefix, L"RemoveDirectory"},
    {Category::File, MatchKind::Prefix, L"FindFirstFile"},
    {Category::File, MatchKind::Prefix, L"FindNextFile"},
    {Category::File, MatchKind::Prefix, L"FindClose"},
    {Category::File, MatchKind::Prefix, L"GetFileAttributes"},
    {Category::File, MatchKind::Prefix, L"SetFileAttributes"},
    {Category::File, MatchKind::Prefix, L"GetFileSize"},
    {Category::File, MatchKind::Prefix, L"SetFilePointer"},
    {Category::File, MatchKind::Prefix, L"SetEndOfFile"},
    {Category::File, MatchKind::Prefix, L"FlushFileBuffers"},
    {Category::File, MatchKind::Prefix, L"LockFile"},
    {Category::File, MatchKind::Prefix, L"UnlockFile"},
    {Category::File, MatchKind::Prefix, L"GetFullPathName"},
    {Category::File, MatchKind::Prefix, L"GetTempPath"},
    {Category::File, MatchKind::Prefix, L"GetTempFileName"},
    {Category::File, MatchKind::Prefix, L"GetCurrentDirectory"},
    {Category::File, MatchKind::Prefix, L"SetCurrentDirectory"},
    {Category::File, MatchKind::Prefix, L"GetDiskFreeSpace"},
    {Category::File, MatchKind::Prefix, L"GetLogicalDrive"},
    {Category::File, MatchKind::Prefix, L"GetDriveType"},
    {Category::File, MatchKind::Prefix, L"GetVolumeInformation"},
    {Category::File, MatchKind::Prefix, L"GetVolumePathName"},
    {Category::File, MatchKind::Prefix, L"QueryDosDevice"},
    {Category::File, MatchKind::Prefix, L"DefineDosDevice"},
    {Category::File, MatchKind::Prefix, L"CreateSymbolicLink"},
    {Category::File, MatchKind::Prefix, L"CreateHardLink"},
    {Category::File, MatchKind::Prefix, L"GetFinalPathNameByHandle"},
    {Category::File, MatchKind::Prefix, L"ReadDirectoryChanges"},
    {Category::File, MatchKind::Prefix, L"CreateFileMapping"},
    {Category::File, MatchKind::Prefix, L"MapViewOfFile"},
    {Category::File, MatchKind::Prefix, L"UnmapViewOfFile"},
    {Category::File, MatchKind::Prefix, L"OpenFileMapping"},
    {Category::File, MatchKind::Prefix, L"GetOverlappedResult"},
    {Category::File, MatchKind::Prefix, L"GetFileInformationByHandle"},
    {Category::File, MatchKind::Prefix, L"SetFileInformationByHandle"},
    {Category::File, MatchKind::Prefix, L"GetFileType"},
    {Category::File, MatchKind::Prefix, L"GetFileTime"},
    {Category::File, MatchKind::Prefix, L"SetFileTime"},
    {Category::File, MatchKind::Prefix, L"GetLongPathName"},
    {Category::File, MatchKind::Prefix, L"GetShortPathName"},
    {Category::File, MatchKind::Prefix, L"SetVolumeMountPoint"},
    {Category::File, MatchKind::Prefix, L"DeleteVolumeMountPoint"},
    {Category::File, MatchKind::Prefix, L"GetVolumeNameForVolumeMountPoint"},
    {Category::File, MatchKind::Exact, L"DeviceIoControl"},

    /* The registry isolation translates the names of redirected handles back
     * into view paths, which is why NtQueryObject is part of the scope. */
    {Category::Registry, MatchKind::Exact, L"NtQueryObject"},
    {Category::Registry, MatchKind::Exact, L"ZwQueryObject"},
    /* Win32 wrappers of the registry entry points: `Reg` followed by an
     * uppercase letter, so `RegisterWaitForSingleObject` is not matched. */
    {Category::Registry, MatchKind::PrefixUpper, L"Reg"},

    /* Win32 wrappers of the named pipe and mailslot entry points. */
    {Category::Network, MatchKind::Prefix, L"CreateNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"ConnectNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"DisconnectNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"CallNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"TransactNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"PeekNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"WaitNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"GetNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"SetNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"ImpersonateNamedPipe"},
    {Category::Network, MatchKind::Prefix, L"CreateMailslot"},
    {Category::Network, MatchKind::Prefix, L"GetMailslot"},
    {Category::Network, MatchKind::Prefix, L"SetMailslot"},
    /* Name resolution, which the socket library routes through kernelbase. */
    {Category::Network, MatchKind::Contains, L"AddrInfo"},
    {Category::Network, MatchKind::Contains, L"NameInfo"},
};

/**
 * @brief Report whether a name starts with an entry point prefix.
 *
 * The prefix has to be followed by an uppercase letter, which is what tells an
 * entry point (`NtCreateFile`, `RegOpenKeyExW`) apart from an unrelated name
 * (`Ntdll` like spellings, `RegisterWaitForSingleObject`).
 *
 * @param[in] name Export name to test.
 * @param[in] prefix Prefix such as `Nt`, `Zw` or `Reg`.
 * @return Whether the name is an entry point with that prefix.
 */
bool StartsWithEntryPoint(const std::wstring& name, const wchar_t* prefix)
{
    const std::size_t length = std::wcslen(prefix);
    if (name.size() <= length || name.compare(0, length, prefix) != 0)
    {
        return false;
    }

    const wchar_t next = name[length];
    return next >= L'A' && next <= L'Z';
}

/**
 * @brief Report whether a name carries a fragment which excludes it.
 *
 * @param[in] name Export name to test.
 * @return Whether the name is excluded from every category.
 */
bool HasExcludedFragment(const std::wstring& name)
{
    for (const auto* fragment : kExcludedFragments)
    {
        if (name.find(fragment) != std::wstring::npos)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Add a category to a list without duplicating it.
 *
 * @param[in,out] categories Category list to extend.
 * @param[in] category Category to add.
 */
void AddCategory(std::vector<Category>& categories, Category category)
{
    if (std::find(categories.begin(), categories.end(), category) == categories.end())
    {
        categories.push_back(category);
    }
}

/**
 * @brief Report whether a name rule matches a name.
 *
 * @param[in] name Export name to test.
 * @param[in] rule Rule to apply.
 * @return Whether the rule matches.
 */
bool MatchesRule(const std::wstring& name, const NameRule& rule)
{
    switch (rule.kind)
    {
    case MatchKind::Exact:
        return name == rule.pattern;
    case MatchKind::Prefix:
        return name.compare(0, std::wcslen(rule.pattern), rule.pattern) == 0;
    case MatchKind::PrefixUpper:
        return StartsWithEntryPoint(name, rule.pattern);
    case MatchKind::Contains:
        return name.find(rule.pattern) != std::wstring::npos;
    }

    return false;
}

} // namespace

std::vector<Category> ClassifyExport(const std::wstring& name)
{
    std::vector<Category> categories;
    if (name.empty() || HasExcludedFragment(name))
    {
        return categories;
    }

    if (StartsWithEntryPoint(name, L"Nt") || StartsWithEntryPoint(name, L"Zw"))
    {
        for (const auto& rule : kNtKeywordRules)
        {
            if (name.find(rule.keyword) != std::wstring::npos)
            {
                AddCategory(categories, rule.category);
            }
        }
    }

    for (const auto& rule : kNameRules)
    {
        if (MatchesRule(name, rule))
        {
            AddCategory(categories, rule.category);
        }
    }

    return categories;
}

bool MatchesCategory(const std::wstring& name, Category category)
{
    const std::vector<Category> categories = ClassifyExport(name);
    return std::find(categories.begin(), categories.end(), category) != categories.end();
}

} // namespace appbox::tracer
