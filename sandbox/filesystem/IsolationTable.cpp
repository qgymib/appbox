#include "IsolationTable.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <cwctype>
#include <exception>
#include <utility>

namespace
{

/**
 * @brief Compare two texts ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when both texts are equal ignoring the case.
 */
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towlower(left[index]) != std::towlower(right[index]))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Order two texts ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when left has to be placed before right.
 */
bool LessIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    const std::size_t shared = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < shared; ++index)
    {
        const wchar_t left_char = std::towlower(left[index]);
        const wchar_t right_char = std::towlower(right[index]);
        if (left_char != right_char)
        {
            return left_char < right_char;
        }
    }
    return left.size() < right.size();
}

/**
 * @brief Split a path into its components.
 *
 * Both separators are accepted, empty components and current directory
 * references are dropped, and a parent reference is rejected because it would
 * leave the virtual filesystem.
 *
 * @param[in] path The path to split.
 * @param[out] components The components in path order.
 * @return true when the path is usable.
 */
bool SplitPath(const std::wstring& path, std::vector<std::wstring>& components)
{
    std::wstring current;
    for (const wchar_t character : path)
    {
        if (character == L'\\' || character == L'/')
        {
            if (current == L"..")
            {
                return false;
            }
            if (!current.empty() && current != L".")
            {
                components.push_back(current);
            }
            current.clear();
            continue;
        }
        current.push_back(character);
    }

    if (current == L"..")
    {
        return false;
    }
    if (!current.empty() && current != L".")
    {
        components.push_back(current);
    }

    return true;
}

/**
 * @brief Translate a virtual path of the isolation file into a view path.
 *
 * The first component of a virtual path is the layer key of the layer the path
 * belongs to; the remaining components are appended to the folder the layer is
 * mapped to. A path whose layer key is not part of the mapping cannot be
 * expressed as a view path and yields an empty result.
 *
 * @param[in] virtual_path The path of the isolation file.
 * @param[in] layers The layers of the view.
 * @return The view path, empty when the path cannot be translated.
 */
std::wstring MapVirtualPathToView(const std::wstring& virtual_path,
                                  const std::vector<appbox::filesystem::IsolationLayer>& layers)
{
    std::vector<std::wstring> components;
    if (!SplitPath(virtual_path, components) || components.empty())
    {
        return {};
    }

    const std::wstring& key = components.front();
    for (const auto& layer : layers)
    {
        if (!EqualsIgnoreCase(layer.layer_key, key))
        {
            continue;
        }

        std::wstring view_path = layer.mapped_nt_path;
        for (std::size_t index = 1; index < components.size(); ++index)
        {
            view_path += L"\\";
            view_path += components[index];
        }
        return appbox::filesystem::IsolationTable::NormalizeViewPath(view_path);
    }

    return {};
}

/**
 * @brief Read a string member of an entry of the isolation file.
 *
 * @param[in] entry The json object of the entry.
 * @param[in] member The member name.
 * @param[out] out The member value.
 * @param[out] error Error description on failure.
 * @return true when the member was read.
 */
bool ReadString(const nlohmann::json& entry, const char* member, std::string& out, std::string& error)
{
    const auto it = entry.find(member);
    if (it == entry.end())
    {
        error = std::string("a filesystem isolation file entry has no '") + member + "' member";
        return false;
    }

    if (!it->is_string())
    {
        error = std::string("the '") + member + "' member of a filesystem isolation file entry is not a string";
        return false;
    }

    out = it->get<std::string>();
    return true;
}

} // namespace

bool appbox::filesystem::IsolationPathLess::operator()(const std::wstring& left, const std::wstring& right) const
{
    return LessIgnoreCase(left, right);
}

bool appbox::filesystem::IsolationTable::Parse(const std::string& text, const std::vector<IsolationLayer>& layers,
                                               std::vector<std::wstring>& unmapped, std::string& error)
{
    std::map<std::wstring, IsolationEntry, IsolationPathLess> entries;
    std::vector<std::wstring>                                 skipped;

    try
    {
        const auto document = nlohmann::json::parse(text);
        if (!document.is_object())
        {
            error = "the filesystem isolation file is not a JSON object";
            return false;
        }

        if (document.value(filesystem_isolation::kVersionKey, 0) != filesystem_isolation::kVersion)
        {
            error = "unsupported filesystem isolation file version";
            return false;
        }

        const auto list = document.find(filesystem_isolation::kEntriesKey);
        if (list != document.end())
        {
            if (!list->is_array())
            {
                error = std::string("the '") + filesystem_isolation::kEntriesKey
                        + "' member of the filesystem isolation file is not a list";
                return false;
            }

            for (const auto& item : *list)
            {
                if (!item.is_object())
                {
                    error = "a filesystem isolation file entry is not an object";
                    return false;
                }

                std::string path;
                std::string kind_token;
                std::string isolation_token;
                if (!ReadString(item, filesystem_isolation::kPathKey, path, error) ||
                    !ReadString(item, filesystem_isolation::kKindKey, kind_token, error) ||
                    !ReadString(item, filesystem_isolation::kIsolationKey, isolation_token, error))
                {
                    return false;
                }

                if (path.empty())
                {
                    error = "a filesystem isolation file entry has an empty path";
                    return false;
                }

                FilesystemEntryKind kind = FilesystemEntryKind::Directory;
                if (!filesystem_isolation::ParseEntryKindToken(kind_token, kind))
                {
                    error = "unknown entry kind '" + kind_token + "' in the filesystem isolation file";
                    return false;
                }

                FilesystemIsolation mode = FilesystemIsolation::Full;
                if (!filesystem_isolation::ParseIsolationToken(isolation_token, mode))
                {
                    error = "unknown isolation mode '" + isolation_token + "' in the filesystem isolation file";
                    return false;
                }

                if (!filesystem_isolation::IsAllowed(mode, kind))
                {
                    error = "the isolation mode '" + isolation_token + "' cannot be used for a "
                            + filesystem_isolation::EntryKindToken(kind) + " in the filesystem isolation file";
                    return false;
                }

                const std::wstring virtual_path = UTF8ToWide(path);
                const std::wstring view_path = MapVirtualPathToView(virtual_path, layers);
                if (view_path.empty())
                {
                    skipped.push_back(virtual_path);
                    continue;
                }

                IsolationEntry entry;
                entry.view_path = view_path;
                entry.kind = kind;
                entry.isolation = mode;
                entries[view_path] = std::move(entry);
            }
        }
    }
    catch (const std::exception& e)
    {
        error = std::string("the filesystem isolation file is not valid: ") + e.what();
        return false;
    }

    entries_ = std::move(entries);
    unmapped.swap(skipped);
    return true;
}

bool appbox::filesystem::IsolationTable::Empty() const
{
    return entries_.empty();
}

std::size_t appbox::filesystem::IsolationTable::Count() const
{
    return entries_.size();
}

bool appbox::filesystem::IsolationTable::Lookup(const std::wstring& view_path, FilesystemIsolation& mode,
                                                FilesystemEntryKind& source_kind) const
{
    /*
     * Walk the path upwards: the closest listed entry covers its whole
     * subtree, which is what makes the mode of a folder reach the entries
     * below it and what lets a folder below override the folder above.
     */
    std::wstring probe = NormalizeViewPath(view_path);
    while (!probe.empty())
    {
        const auto it = entries_.find(probe);
        if (it != entries_.end())
        {
            mode = it->second.isolation;
            source_kind = it->second.kind;
            return true;
        }

        const auto separator = probe.find_last_of(L'\\');
        if (separator == std::wstring::npos)
        {
            break;
        }
        probe.erase(separator);
    }

    return false;
}

std::wstring appbox::filesystem::IsolationTable::NormalizeViewPath(const std::wstring& path)
{
    std::vector<std::wstring> components;
    if (!SplitPath(path, components) || components.empty())
    {
        return {};
    }

    /*
     * Every view path is absolute, so the normalized form is rebuilt with a
     * leading separator; that is also what makes the drive root (`\??\C:\`)
     * and the folder it addresses (`\??\C:`) the same key.
     */
    std::wstring normalized;
    for (const auto& component : components)
    {
        normalized += L"\\";
        normalized += component;
    }
    return normalized;
}

std::wstring appbox::filesystem::IsolationTable::LayerKeyOf(const std::wstring& host_nt_path)
{
    std::wstring path = host_nt_path;
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
    {
        path.pop_back();
    }

    const auto separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
    {
        return path;
    }
    return path.substr(separator + 1);
}
