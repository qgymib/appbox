#ifndef APPBOX_SANDBOX_FILESYSTEM_STREAMNAME_HPP
#define APPBOX_SANDBOX_FILESYSTEM_STREAMNAME_HPP

#include <cstddef>
#include <string>

namespace appbox
{
namespace filesystem
{

/**
 * @brief Whether a character can name the drive of a path.
 * @param[in] character Character to inspect.
 * @return true when the character is a letter.
 */
inline bool IsDriveLetter(wchar_t character)
{
    return (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z');
}

/**
 * @brief Position of the separator between the last component and its stream.
 *
 * A path names an alternate data stream when its last component carries a
 * colon, which is how the file system addresses the streams of a file:
 * `\??\C:\dir\file.txt:stream` names the stream `stream` of the file
 * `file.txt`.
 *
 * The colon of the drive is not such a separator: the component of the drive is
 * a letter followed by its colon and nothing else (`\??\C:`), which addresses
 * the drive and not the stream of a file which is named `C`. The component of a
 * file which carries a stream is longer than that, because the name of the file
 * stands before the colon.
 *
 * A stream name is always the last component of a path: the file system refuses
 * a path which continues below a stream, so no component before the last one is
 * inspected.
 *
 * @param[in] path Path to inspect.
 * @return Index of the separator, `std::wstring::npos` when the path names no
 *         stream.
 */
inline std::size_t StreamNameSeparator(const std::wstring& path)
{
    const auto last_separator = path.find_last_of(L'\\');
    const auto name_begin = last_separator == std::wstring::npos ? 0 : last_separator + 1;

    const auto colon = path.find(L':', name_begin);
    if (colon == std::wstring::npos)
    {
        return std::wstring::npos;
    }

    /* The component of the drive, see above. */
    if (colon == name_begin + 1 && colon + 1 == path.size() && IsDriveLetter(path[name_begin]))
    {
        return std::wstring::npos;
    }

    return colon;
}

/**
 * @brief Whether a path names an alternate data stream.
 *
 * @param[in] path Path to inspect.
 * @return true when the last component of the path carries a stream name.
 */
inline bool CarriesStreamName(const std::wstring& path)
{
    return StreamNameSeparator(path) != std::wstring::npos;
}

/**
 * @brief Name of the alternate data stream of a path.
 *
 * @param[in] path Path to inspect.
 * @return The name of the stream, empty when the path names no stream.
 */
inline std::wstring StreamNameOf(const std::wstring& path)
{
    const auto separator = StreamNameSeparator(path);
    if (separator == std::wstring::npos)
    {
        return {};
    }
    return path.substr(separator + 1);
}

/**
 * @brief Path of the entry which carries the stream of a path.
 *
 * The entry of `\??\C:\dir\file.txt:stream` is `\??\C:\dir\file.txt`: a stream
 * cannot exist without the file which carries it, so the file is the entry the
 * view decides the isolation mode, the visibility and the layer of a
 * modification from.
 *
 * @param[in] path Path to reduce.
 * @return The path of the entry, the path itself when it names no stream.
 */
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

#endif // APPBOX_SANDBOX_FILESYSTEM_STREAMNAME_HPP
