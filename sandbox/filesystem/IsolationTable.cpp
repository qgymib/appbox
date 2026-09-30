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
std::wstring MapVirtualPathToView(const std::wstring&                                    virtual_path,
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
        /*
         * The document is read as the structure of its schema
         * (`common/FilesystemIsolation.hpp`) and never member by member, so a
         * member which is missing, which is of another type, which names an
         * unknown mode or which names a mode the kind cannot hold is refused
         * while the file is read.
         */
        const auto document = nlohmann::json::parse(text).get<filesystem_isolation::Document>();

        if (document.version != filesystem_isolation::kVersion)
        {
            error = "unsupported filesystem isolation file version";
            return false;
        }

        for (const auto& item : document.entries)
        {
            const std::wstring virtual_path = UTF8ToWide(item.path);

            /*
             * An entry without a path is the root of the view, which is the
             * folder every path no other entry covers belongs to. It has no
             * layer key to translate, so it is stored under the empty key the
             * lookup falls back to; every other entry is translated into the
             * view path of the layer it names.
             */
            const std::wstring view_path =
                virtual_path.empty() ? std::wstring() : MapVirtualPathToView(virtual_path, layers);
            if (view_path.empty() && !virtual_path.empty())
            {
                skipped.push_back(virtual_path);
                continue;
            }

            IsolationEntry entry;
            entry.view_path = view_path;
            entry.kind = item.kind;
            entry.isolation = item.isolation;
            entries[view_path] = std::move(entry);
        }
    }
    catch (const IsolationDocumentError& e)
    {
        error = e.what();
        return false;
    }
    catch (const std::exception& e)
    {
        error = std::string("the filesystem isolation file is not valid: ") + e.what();
        return false;
    }

    /*
     * The document is applied as the layer it describes: the modes it lists
     * replace the modes of the same path which the layers below it set, while
     * the entries of the paths it does not list stay untouched. The document
     * was collected into a local table, so a document which cannot be used
     * leaves the table as it was.
     */
    for (auto& entry : entries)
    {
        entries_[entry.first] = std::move(entry.second);
    }

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
     * below it and what lets a folder below override the folder above. The
     * walk ends on the root of the view, which is the entry stored under the
     * empty key and which covers every path no listed folder names.
     */
    std::wstring probe = NormalizeViewPath(view_path);
    for (;;)
    {
        const auto it = entries_.find(probe);
        if (it != entries_.end())
        {
            mode = it->second.isolation;
            source_kind = it->second.kind;
            return true;
        }

        if (probe.empty())
        {
            break;
        }

        const auto separator = probe.find_last_of(L'\\');
        if (separator == std::wstring::npos)
        {
            probe.clear();
            continue;
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
