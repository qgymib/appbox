#include "RootMap.hpp"
#include "KeyPath.hpp"
#include "WString.hpp"

namespace
{

/**
 * @brief Whether two names are equal ignoring the case.
 * @param[in] left Left name.
 * @param[in] right Right name.
 * @return true when both names are equal.
 */
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    std::wstring suffix;
    return appbox::PrefixCompareExchange(left, right, L"", true, suffix) && suffix.empty();
}

/**
 * @brief The root key name a hive relative path starts with.
 * @param[in] relative The hive relative path.
 * @return The name of the first component, empty for an empty path.
 */
std::wstring FirstComponent(const std::wstring& relative)
{
    const auto separator = relative.find(L'\\');
    if (separator == std::wstring::npos)
    {
        return relative;
    }
    return relative.substr(0, separator);
}

/**
 * @brief The part of a hive relative path behind its first component.
 * @param[in] relative The hive relative path.
 * @return The remaining path, empty when the path has a single component.
 */
std::wstring RemainingPath(const std::wstring& relative)
{
    const auto separator = relative.find(L'\\');
    if (separator == std::wstring::npos)
    {
        return {};
    }
    return relative.substr(separator + 1);
}

} // namespace

const std::vector<appbox::registry::RootKeyPrefix>& appbox::registry::RootKeyPrefixes()
{
    /*
     * Descending prefix length: the longest prefix has to win, so the classes
     * of HKCR and the hardware profile of HKCC are tested before the machine
     * root they live below.
     */
    static const std::vector<RootKeyPrefix> prefixes = {
        { L"HKEY_CURRENT_CONFIG",
          L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\HARDWARE PROFILES\\CURRENT" },
        { L"HKEY_CLASSES_ROOT", L"\\REGISTRY\\MACHINE\\SOFTWARE\\CLASSES" },
        { L"HKEY_LOCAL_MACHINE", L"\\REGISTRY\\MACHINE" },
        { L"HKEY_USERS", L"\\REGISTRY\\USER" },
    };
    return prefixes;
}

const std::vector<std::wstring>& appbox::registry::HiveRootKeyNames()
{
    /* The display order of the registry view, with the current user root of
     * the prefix table filled in. */
    static const std::vector<std::wstring> names = {
        L"HKEY_CLASSES_ROOT", L"HKEY_CURRENT_USER",   L"HKEY_LOCAL_MACHINE",
        L"HKEY_USERS",        L"HKEY_CURRENT_CONFIG",
    };
    return names;
}

bool appbox::registry::MapViewPathToHive(const std::wstring& view_path, const std::wstring& hkcu_prefix,
                                         std::wstring& relative)
{
    /*
     * The current user root is matched first: its NT path is longer than the
     * \REGISTRY\USER prefix of HKEY_USERS, so the two can never be confused.
     */
    std::wstring rest;
    if (!hkcu_prefix.empty() && StripKeyPrefix(view_path, hkcu_prefix, rest))
    {
        relative = JoinKeyPath(L"HKEY_CURRENT_USER", rest);
        return true;
    }

    for (const auto& prefix : RootKeyPrefixes())
    {
        if (StripKeyPrefix(view_path, prefix.nt_prefix, rest))
        {
            relative = JoinKeyPath(prefix.root_name, rest);
            return true;
        }
    }

    return false;
}

bool appbox::registry::MapHivePathToView(const std::wstring& relative, const std::wstring& hkcu_prefix,
                                         std::wstring& view_path)
{
    const auto name = FirstComponent(relative);
    const auto rest = RemainingPath(relative);
    if (name.empty())
    {
        return false;
    }

    if (EqualsIgnoreCase(name, L"HKEY_CURRENT_USER"))
    {
        if (hkcu_prefix.empty())
        {
            return false;
        }
        view_path = JoinKeyPath(hkcu_prefix, rest);
        return true;
    }

    for (const auto& prefix : RootKeyPrefixes())
    {
        if (EqualsIgnoreCase(name, prefix.root_name))
        {
            view_path = JoinKeyPath(prefix.nt_prefix, rest);
            return true;
        }
    }

    return false;
}

std::wstring appbox::registry::HiveRootKeyName(const std::wstring& relative)
{
    const auto name = FirstComponent(relative);
    if (name.empty())
    {
        return {};
    }

    if (EqualsIgnoreCase(name, L"HKEY_CURRENT_USER"))
    {
        return L"HKEY_CURRENT_USER";
    }

    for (const auto& prefix : RootKeyPrefixes())
    {
        if (EqualsIgnoreCase(name, prefix.root_name))
        {
            return prefix.root_name;
        }
    }

    return {};
}
