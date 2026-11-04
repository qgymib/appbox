#ifndef APPBOX_COMMON_DETAILS_FILESYSTEM_ISOLATION_TABLE_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_FILESYSTEM_ISOLATION_TABLE_DETAIL_HPP

/*
 * Implementation of `common/FilesystemIsolationTable.hpp`: the parsing of the
 * isolation file of one layer, the translation of its virtual paths into paths of
 * the view and the lookup of the mode which applies to a path. The header is
 * included by the public header and is not meant to be included on its own.
 */

#include "FilesystemIsolationTable.hpp"
#include "PathText.hpp"
#include "WString.hpp"
#include <cstddef>
#include <cwctype>
#include <map>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <utility>
#include <vector>

namespace appbox
{
namespace detail
{

/**
 * @brief Compare two texts ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when both texts are equal ignoring the case.
 */
inline bool IsolationPathsEqual(const std::wstring& left, const std::wstring& right) noexcept
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
inline bool IsolationPathCompare(const std::wstring& left, const std::wstring& right) noexcept
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
 * @return The components in path order, or `kInvalidArgument` when the path
 *         carries a parent reference.
 */
inline tl::expected<std::vector<std::wstring>, ErrorCode> SplitIsolationPath(const std::wstring& path) noexcept
{
    std::vector<std::wstring> components;
    std::wstring              current;

    const auto flush = [&components, &current]() -> bool {
        if (current == L"..")
        {
            return false;
        }
        if (!current.empty() && current != L".")
        {
            components.push_back(current);
        }
        current.clear();
        return true;
    };

    for (const wchar_t character : path)
    {
        if (character == L'\\' || character == L'/')
        {
            if (!flush())
            {
                return tl::unexpected<ErrorCode>(ErrorCode::kInvalidArgument);
            }
            continue;
        }
        current.push_back(character);
    }

    if (!flush())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kInvalidArgument);
    }

    return components;
}

/**
 * @brief Translate a virtual path of the isolation file into a view path.
 *
 * The first component of a virtual path is the layer key of the layer the path
 * belongs to; the remaining components are appended to the folder the layer is
 * mapped to. A path whose layer key is not part of the mapping cannot be
 * expressed as a view path and is reported as `kLayerKeyUnmapped`. A path
 * without a component carries no layer key at all and is answered with an empty
 * view path, which is the key of the root of the view: the empty answer of that
 * case is a value and not a failure.
 *
 * @param[in] virtual_path The path of the isolation file.
 * @param[in] layers The layers of the view.
 * @return The view path, or `kLayerKeyUnmapped` when the layer key of the path
 *         is not part of the mapping.
 */
inline tl::expected<std::wstring, ErrorCode> IsolationVirtualPathToView(
    const std::wstring& virtual_path, const std::vector<appbox::filesystem::IsolationLayer>& layers) noexcept
{
    const auto components = SplitIsolationPath(virtual_path);
    if (!components)
    {
        return tl::unexpected<ErrorCode>(components.error());
    }
    if (components->empty())
    {
        return std::wstring();
    }

    const std::wstring& key = components->front();
    for (const auto& layer : layers)
    {
        if (!IsolationPathsEqual(layer.layer_key, key))
        {
            continue;
        }

        std::wstring view_path = layer.mapped_nt_path;
        for (std::size_t index = 1; index < components->size(); ++index)
        {
            view_path += L"\\";
            view_path += (*components)[index];
        }
        return appbox::filesystem::IsolationTable::NormalizeViewPath(view_path);
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kLayerKeyUnmapped);
}

} // namespace detail

namespace filesystem
{

/* Definition of the contract on the declaration. */
inline bool IsolationPathLess::operator()(const std::wstring& left, const std::wstring& right) const noexcept
{
    return appbox::detail::IsolationPathCompare(left, right);
}

/* Definition of the contract on the declaration. */
inline tl::expected<IsolationTable::ParseOutcome, ErrorCode> IsolationTable::Parse(
    std::string_view text, const std::vector<IsolationLayer>& layers) noexcept
{
    /*
     * The document is read as the structure of its schema
     * (`common/FilesystemIsolation.hpp`) and never member by member, so a member
     * which is missing, which is of another type, which names an unknown mode or
     * which names a mode the kind cannot hold is refused by the reader of the
     * document.
     */
    const auto document = filesystem_isolation::ParseDocument(text);
    if (!document)
    {
        return tl::unexpected<ErrorCode>(document.error());
    }

    std::map<std::wstring, IsolationEntry, IsolationPathLess> entries;
    ParseOutcome                                              outcome;

    for (const auto& item : document->entries)
    {
        /*
         * The path of an entry is stored as UTF-8 and the table is keyed by the
         * wide form of it, so a path the code page cannot carry leaves the
         * entry with no key to be stored under. The class of that failure is the
         * one of the conversion, which is what the reader reports.
         */
        const auto converted_path = UTF8ToWide(item.path);
        if (!converted_path)
        {
            return tl::unexpected<ErrorCode>(converted_path.error());
        }
        const std::wstring& virtual_path = *converted_path;

        /*
         * An entry without a path is the root of the view, which is the folder
         * every path no other entry covers belongs to. It has no layer key to
         * translate, so it is stored under the empty key the lookup falls back
         * to; every other entry is translated into the view path of the layer it
         * names.
         */
        const auto view_path = appbox::detail::IsolationVirtualPathToView(virtual_path, layers);
        if (!view_path)
        {
            if (virtual_path.empty())
            {
                return tl::unexpected<ErrorCode>(view_path.error());
            }

            /* The layer of the entry is not part of the run, so it is skipped. */
            outcome.unmapped.push_back(virtual_path);
            continue;
        }

        IsolationEntry entry;
        entry.view_path = *view_path;
        entry.kind = item.kind;
        entry.isolation = item.isolation;
        entries[entry.view_path] = std::move(entry);
    }

    /*
     * The document is applied as the layer it describes: the modes it lists
     * replace the modes of the same path which the layers below it set, while
     * the entries of the paths it does not list stay untouched. The document was
     * collected into a local table, so a document which cannot be used leaves
     * the table as it was.
     */
    for (auto& entry : entries)
    {
        entries_[entry.first] = std::move(entry.second);
    }

    return outcome;
}

/* Definition of the contract on the declaration. */
inline bool IsolationTable::Empty() const noexcept
{
    return entries_.empty();
}

/* Definition of the contract on the declaration. */
inline std::size_t IsolationTable::Count() const noexcept
{
    return entries_.size();
}

/* Definition of the contract on the declaration. */
inline tl::expected<IsolationTable::LookupResult, ErrorCode> IsolationTable::Lookup(
    const std::wstring& view_path) const noexcept
{
    /*
     * Walk the path upwards: the closest listed entry covers its whole subtree,
     * which is what makes the mode of a folder reach the entries below it and
     * what lets a folder below override the folder above. The walk ends on the
     * root of the view, which is the entry stored under the empty key and which
     * covers every path no listed folder names.
     *
     * The path is probed as it is before the walk starts, so a document which
     * names a stream itself is honoured: the stream is the most specific entry
     * of the file which carries it. The entry of the file follows, because a
     * stream belongs to that file: the mode of `file.txt` covers
     * `file.txt:stream` as well, and only a path the file does not cover follows
     * the folder above it.
     */
    std::wstring probe = NormalizeViewPath(view_path);
    for (;;)
    {
        const auto it = entries_.find(probe);
        if (it != entries_.end())
        {
            LookupResult result;
            result.mode = it->second.isolation;
            result.source_kind = it->second.kind;
            return result;
        }

        if (probe.empty())
        {
            break;
        }

        const auto entry = EntryPathOfStream(probe);
        if (entry != probe)
        {
            probe = entry;
            continue;
        }

        const auto separator = probe.find_last_of(L'\\');
        if (separator == std::wstring::npos)
        {
            probe.clear();
            continue;
        }
        probe.erase(separator);
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kIsolationEntryNotListed);
}

/* Definition of the contract on the declaration. */
inline std::wstring IsolationTable::NormalizeViewPath(const std::wstring& path) noexcept
{
    const auto components = appbox::detail::SplitIsolationPath(path);
    if (!components || components->empty())
    {
        return {};
    }

    /*
     * Every view path is absolute, so the normalized form is rebuilt with a
     * leading separator; that is also what makes the drive root (`\??\C:\`) and
     * the folder it addresses (`\??\C:`) the same key.
     */
    std::wstring normalized;
    for (const auto& component : *components)
    {
        normalized += L"\\";
        normalized += component;
    }
    return normalized;
}

/* Definition of the contract on the declaration. */
inline std::wstring IsolationTable::LayerKeyOf(const std::wstring& host_nt_path) noexcept
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

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_FILESYSTEM_ISOLATION_TABLE_DETAIL_HPP
