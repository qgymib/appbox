#ifndef APPBOX_COMMON_DETAILS_PATH_TEXT_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_PATH_TEXT_DETAIL_HPP

/*
 * Implementation of `common/PathText.hpp`: the directory name of a path, the
 * splitting of a path into its prefixes and the helpers which tell the stream
 * name of a path apart from its entry. The header is included by the public
 * header and is not meant to be included on its own.
 */

#include "PathText.hpp"
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{
namespace detail
{

/**
 * @brief Split a text on a delimiter, keeping the empty parts.
 *
 * The helper is the splitting rule `DirName()` needs, spelled out here because
 * `common/WString.hpp` brings `<windows.h>` with it: the path text of this
 * header is included by translation units of the sandbox which only need the
 * text, and a Windows header in that path would pull the definitions of the
 * platform into every one of them.
 *
 * The text is split on every hit of the delimiter, the parts before, between
 * and behind the hits are kept even when they are empty, and a text which
 * carries no delimiter yields one part which is the text itself.
 *
 * @param[in] text Text to split.
 * @param[in] delimiter Delimiter to split on.
 * @return The parts of the text, in the order they appear.
 */
inline std::vector<std::wstring> SplitKeepingEmpty(const std::wstring& text, const std::wstring& delimiter)
{
    std::vector<std::wstring> result;

    if (delimiter.empty())
    {
        result.push_back(text);
        return result;
    }

    std::wstring::size_type start = 0;
    std::wstring::size_type pos = 0;

    while ((pos = text.find(delimiter, start)) != std::wstring::npos)
    {
        result.emplace_back(text.substr(start, pos - start));
        start = pos + delimiter.size();
    }

    /* Include the last part. */
    result.emplace_back(text.substr(start));

    return result;
}

} // namespace detail

namespace filesystem
{

/* Definition of the contract on the declaration. */
inline std::wstring DirName(const std::wstring& path)
{
    assert(path[0] == L'\\');

    /* Remote trailing slash */
    auto cp_path = path;
    while (cp_path.back() == L'\\')
    {
        cp_path.pop_back();
    }

    auto vec = appbox::detail::SplitKeepingEmpty(cp_path, L"\\");
    auto vec_sz = vec.size();
    if (vec_sz > 3)
    {
        vec.pop_back();
        vec_sz--;
    }

    std::wstring ret;
    for (size_t i = 1; i < vec_sz; ++i)
    {
        ret += L"\\" + vec[i];
    }

    return ret;
}

/* Definition of the contract on the declaration. */
inline std::vector<std::wstring> Sequence(const std::wstring& path, std::size_t offset, const std::wstring& delimiter,
                                          bool bIncludeLast)
{
    std::vector<std::wstring> result;

    std::wstring::size_type pos;
    while ((pos = path.find(delimiter, offset)) != std::wstring::npos)
    {
        result.push_back(path.substr(0, pos));
        offset = pos + 1;
    }
    if (bIncludeLast)
    {
        result.push_back(path);
    }

    return result;
}

/* Definition of the contract on the declaration. */
inline bool IsDriveLetter(wchar_t character)
{
    return (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z');
}

/* Definition of the contract on the declaration. */
inline std::size_t StreamNameSeparator(const std::wstring& path)
{
    const auto last_separator = path.find_last_of(L'\\');
    const auto name_begin = last_separator == std::wstring::npos ? 0 : last_separator + 1;

    const auto colon = path.find(L':', name_begin);
    if (colon == std::wstring::npos)
    {
        return std::wstring::npos;
    }

    /* The component of the drive, see the declaration. */
    if (colon == name_begin + 1 && colon + 1 == path.size() && IsDriveLetter(path[name_begin]))
    {
        return std::wstring::npos;
    }

    return colon;
}

/* Definition of the contract on the declaration. */
inline bool CarriesStreamName(const std::wstring& path)
{
    return StreamNameSeparator(path) != std::wstring::npos;
}

/* Definition of the contract on the declaration. */
inline std::wstring StreamNameOf(const std::wstring& path)
{
    const auto separator = StreamNameSeparator(path);
    if (separator == std::wstring::npos)
    {
        return {};
    }
    return path.substr(separator + 1);
}

/* Definition of the contract on the declaration. */
inline std::wstring EntryPathOfStream(const std::wstring& path)
{
    const auto separator = StreamNameSeparator(path);
    if (separator == std::wstring::npos)
    {
        return path;
    }
    return path.substr(0, separator);
}

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_PATH_TEXT_DETAIL_HPP
