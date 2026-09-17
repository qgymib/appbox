#include "KeyPath.hpp"
#include "WString.hpp"

bool appbox::registry::StripKeyPrefix(const std::wstring& path, const std::wstring& prefix,
                                      std::wstring& relative)
{
    /*
     * Reuse the case insensitive prefix compare of the common module. The
     * replacement is empty, so the output holds the un-matched suffix.
     */
    std::wstring suffix;
    if (!appbox::PrefixCompareExchange(path, prefix, L"", true, suffix))
    {
        return false;
    }

    /* The prefix must end on a component boundary. */
    if (!suffix.empty() && suffix.front() != L'\\')
    {
        return false;
    }

    relative.assign(suffix, suffix.empty() ? 0 : 1, std::wstring::npos);
    return true;
}

std::wstring appbox::registry::JoinKeyPath(const std::wstring& root, const std::wstring& name)
{
    if (root.empty())
    {
        return name;
    }
    if (name.empty())
    {
        return root;
    }
    return root + L"\\" + name;
}
