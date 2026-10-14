#include "SnapshotModel.hpp"
#include <cwctype>

namespace appbox
{

namespace snapshot
{

std::wstring FoldText(const std::wstring& text)
{
    std::wstring folded;
    folded.reserve(text.size());

    for (const auto character : text)
    {
        folded.push_back(static_cast<wchar_t>(std::towupper(static_cast<std::wint_t>(character))));
    }

    return folded;
}

std::wstring FileLookupKey(const std::string& preset_id, const std::wstring& path)
{
    /*
     * The preset identifier is ASCII and part of the model, so it is copied as
     * it is; only the path below it is folded, because it names entries of the
     * host filesystem.
     */
    std::wstring key(preset_id.begin(), preset_id.end());
    key.push_back(L'\n');
    key += FoldText(path);
    return key;
}

std::wstring RegistryKeyLookupKey(const std::wstring& path)
{
    return FoldText(path);
}

std::wstring RegistryValueLookupKey(const std::wstring& path, const std::wstring& name)
{
    auto key = FoldText(path);
    key.push_back(L'\n');
    key += FoldText(name);
    return key;
}

std::wstring EnvironmentLookupKey(const std::wstring& name)
{
    return FoldText(name);
}

} // namespace snapshot

} // namespace appbox
