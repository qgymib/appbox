#include "Whiteout.hpp"
#include "KeyPath.hpp"

std::wstring appbox::registry::WhiteoutKeyPath(const std::wstring& relative)
{
    const std::wstring store = JoinKeyPath(registry_whiteout::kStoreKey, registry_whiteout::kKeysKey);
    return JoinKeyPath(store, relative);
}

std::wstring appbox::registry::WhiteoutValueKeyPath(const std::wstring& relative)
{
    const std::wstring store = JoinKeyPath(registry_whiteout::kStoreKey, registry_whiteout::kValuesKey);
    return JoinKeyPath(store, relative);
}

void appbox::registry::KeyPathPrefixes(const std::wstring& relative, std::vector<std::wstring>& prefixes)
{
    prefixes.clear();
    if (relative.empty())
    {
        return;
    }

    std::wstring probe = relative;
    for (;;)
    {
        prefixes.push_back(probe);

        const auto separator = probe.rfind(L'\\');
        if (separator == std::wstring::npos)
        {
            return;
        }
        probe.erase(separator);
    }
}
