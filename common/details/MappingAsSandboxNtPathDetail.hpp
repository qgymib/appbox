#ifndef APPBOX_COMMON_DETAILS_MAPPING_AS_SANDBOX_NT_PATH_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_MAPPING_AS_SANDBOX_NT_PATH_DETAIL_HPP

/*
 * Implementation of `common/MappingAsSandboxNtPath.hpp`: the parsing of a DOS
 * style NT path, the normalization of the components below its drive and the
 * rebase of a path of the view below the root of the upper layer. The header is
 * included by the public header and is not meant to be included on its own.
 */

#include "MappingAsSandboxNtPath.hpp"
#include <cstddef>
#include <cwctype>
#include <string>
#include <tl/expected.hpp>
#include <utility>
#include <vector>

namespace appbox
{
namespace detail
{

/**
 * @brief A DOS style NT path of a drive, split at the colon of the drive.
 *
 * The drive is the upper case letter, so two spellings of the same drive
 * compare equal, and the remainder holds the components below the root of the
 * drive, without the separator which follows the colon.
 */
struct MappingDosDrive
{
    /** Upper case letter of the drive, for example `C`. */
    wchar_t drive = 0;

    /** Components below the root of the drive, without a leading separator. */
    std::wstring remainder;
};

/**
 * @brief Whether a text starts with a prefix, ignoring the case.
 * @param[in] text Text to inspect.
 * @param[in] prefix Prefix to compare against.
 * @return true when the text starts with the prefix.
 */
inline bool MappingStartsWithIgnoreCase(const std::wstring& text, const std::wstring& prefix) noexcept
{
    if (text.size() < prefix.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index)
    {
        if (std::towlower(text[index]) != std::towlower(prefix[index]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Whether a character separates two components of a path.
 * @param[in] character Character to inspect.
 * @return true when the character is a separator.
 */
inline bool IsMappingSeparator(wchar_t character) noexcept
{
    return character == L'\\' || character == L'/';
}

/**
 * @brief Whether a character can name the drive of a path.
 * @param[in] character Character to inspect.
 * @return true when the character is a letter.
 */
inline bool IsMappingDriveLetter(wchar_t character) noexcept
{
    return (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z');
}

/**
 * @brief Parse a DOS style NT path into its drive and its remainder.
 *
 * Only the two namespaces which open a drive are understood, `\??\X:\...` and
 * `\GLOBAL??\X:\...`, and the separator behind the colon is required: `X:`
 * alone addresses the current directory of the drive and names no entry.
 *
 * @param[in] path Path to parse.
 * @return The drive and the remainder of the path, or `kPathNotDosDrivePath`
 *         when the path is no absolute path of a drive in that namespace.
 */
inline tl::expected<MappingDosDrive, ErrorCode> ParseMappingDosDrivePath(const std::wstring& path) noexcept
{
    std::size_t prefix_size = 0;

    if (MappingStartsWithIgnoreCase(path, L"\\??\\"))
    {
        prefix_size = 4;
    }
    else if (MappingStartsWithIgnoreCase(path, L"\\GLOBAL??\\"))
    {
        prefix_size = 10;
    }
    else
    {
        return tl::make_unexpected(ErrorCode::kPathNotDosDrivePath);
    }

    /* The path has to carry a drive letter, its colon and a separator. */
    if (path.size() < prefix_size + 3)
    {
        return tl::make_unexpected(ErrorCode::kPathNotDosDrivePath);
    }

    const wchar_t letter = path[prefix_size];
    if (!IsMappingDriveLetter(letter) || path[prefix_size + 1] != L':' || !IsMappingSeparator(path[prefix_size + 2]))
    {
        return tl::make_unexpected(ErrorCode::kPathNotDosDrivePath);
    }

    MappingDosDrive drive;
    drive.drive = static_cast<wchar_t>(std::towupper(letter));
    drive.remainder = path.substr(prefix_size + 3);
    return drive;
}

/**
 * @brief Normalize the components of a path.
 *
 * Empty components and current directory references are dropped and a parent
 * reference removes the component before it, which is what the file system
 * does while it walks a path. A parent reference which climbs above the root of
 * the drive names no entry of the view, so the call fails instead of answering
 * a path outside of it.
 *
 * @param[in] path Components to normalize.
 * @return The components without a separator between them, or
 *         `kPathEscapesDriveRoot` when the path leaves its drive.
 */
inline tl::expected<std::wstring, ErrorCode> NormalizeMappingRelativePath(const std::wstring& path) noexcept
{
    std::vector<std::wstring> parts;

    std::size_t index = 0;
    while (index < path.size())
    {
        while (index < path.size() && IsMappingSeparator(path[index]))
        {
            ++index;
        }

        const std::size_t start = index;
        while (index < path.size() && !IsMappingSeparator(path[index]))
        {
            ++index;
        }

        if (start == index)
        {
            break;
        }

        std::wstring part = path.substr(start, index - start);

        if (part == L"." || part.empty())
        {
            continue;
        }

        if (part == L"..")
        {
            if (parts.empty())
            {
                /* The reference climbs above the root of the drive. */
                return tl::make_unexpected(ErrorCode::kPathEscapesDriveRoot);
            }
            parts.pop_back();
            continue;
        }

        parts.push_back(std::move(part));
    }

    std::wstring normalized;
    for (std::size_t position = 0; position < parts.size(); ++position)
    {
        if (position != 0)
        {
            normalized += L'\\';
        }
        normalized += parts[position];
    }

    return normalized;
}

/**
 * @brief Remove the separators at the end of a path.
 *
 * The root of a drive keeps its separator, so a path which is the root of a
 * volume is left unchanged.
 *
 * @param[in] path Path to trim.
 * @return The path without the separators at its end.
 */
inline std::wstring TrimMappingTrailingSeparators(std::wstring path) noexcept
{
    while (path.size() > 1 && IsMappingSeparator(path.back()))
    {
        path.pop_back();
    }
    return path;
}

} // namespace detail

/* Definition of the contract on the declaration. */
inline tl::expected<std::wstring, ErrorCode> MappingPathInSandbox(const std::wstring& path,
                                                                  const std::wstring& sandbox) noexcept
{
    const auto parsed = detail::ParseMappingDosDrivePath(path);
    if (!parsed)
    {
        return tl::make_unexpected(parsed.error());
    }

    /* The root of the layer has to be a path of a drive as well. */
    if (!detail::ParseMappingDosDrivePath(sandbox))
    {
        return tl::make_unexpected(ErrorCode::kPathNotDosDrivePath);
    }

    const auto normalized_remainder = detail::NormalizeMappingRelativePath(parsed->remainder);
    if (!normalized_remainder)
    {
        return tl::make_unexpected(normalized_remainder.error());
    }

    const std::wstring root = detail::TrimMappingTrailingSeparators(sandbox);

    std::wstring out = root;
    out += L'\\';
    out += parsed->drive;

    if (!normalized_remainder->empty())
    {
        out += L'\\';
        out += *normalized_remainder;
    }

    return out;
}

} // namespace appbox

#endif // APPBOX_COMMON_DETAILS_MAPPING_AS_SANDBOX_NT_PATH_DETAIL_HPP
