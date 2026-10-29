#ifndef APPBOX_COMMON_PATH_TEXT_HPP
#define APPBOX_COMMON_PATH_TEXT_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{
namespace filesystem
{

/**
 * @brief Get the directory name of a path.
 *
 * The path is the form the file system reports for a name, so it starts with a
 * separator and names the components of one volume, for example
 * `\Windows\System32\ntdll.dll`. The trailing separators are dropped, the last
 * component is removed and the remaining ones are joined again, which answers
 * `\Windows\System32` for the example above.
 *
 * A path which names no component below its first one has no directory of its
 * own, so the result is empty: the caller has to keep the root of the volume
 * from the path it passed.
 *
 * @param[in] path The path to get the directory name from.
 * @return The directory name of the path.
 */
inline std::wstring DirName(const std::wstring& path);

/**
 * @brief Split a string path into a sequence of directory paths.
 *
 * Every hit of the delimiter adds the prefix of the path up to it, so the
 * result holds one path per component, each of them still carrying the text
 * before the component: `\Windows\System32` yields `\Windows` and
 * `\Windows\System32`. The first component of the path, which is the root of
 * the volume, is therefore part of every element of the result.
 *
 * The caller decides whether the path itself is part of the result: with
 * \p bIncludeLast the last element is the whole path, otherwise the result
 * stops on the component before it.
 *
 * @param[in] path The input string to split.
 * @param[in] offset The starting position for splitting.
 * @param[in] delimiter The delimiter string used for splitting.
 * @param[in] bIncludeLast Whether to include the last path in the sequence.
 * @return A vector of directory paths.
 */
inline std::vector<std::wstring> Sequence(const std::wstring& path, std::size_t offset, const std::wstring& delimiter,
                                          bool bIncludeLast);

/**
 * @brief Whether a character can name the drive of a path.
 * @param[in] character Character to inspect.
 * @return true when the character is a letter.
 */
inline bool IsDriveLetter(wchar_t character);

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
inline std::size_t StreamNameSeparator(const std::wstring& path);

/**
 * @brief Whether a path names an alternate data stream.
 *
 * @param[in] path Path to inspect.
 * @return true when the last component of the path carries a stream name.
 */
inline bool CarriesStreamName(const std::wstring& path);

/**
 * @brief Name of the alternate data stream of a path.
 *
 * @param[in] path Path to inspect.
 * @return The name of the stream, empty when the path names no stream.
 */
inline std::wstring StreamNameOf(const std::wstring& path);

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
inline std::wstring EntryPathOfStream(const std::wstring& path);

} // namespace filesystem
} // namespace appbox

/*
 * The path text helpers are small enough to be defined next to their
 * declaration, yet the project keeps every implementation in a detail header:
 * the header below is the implementation of the declarations above and is
 * included by this header alone.
 */
#include "details/PathTextDetail.hpp"

#endif // APPBOX_COMMON_PATH_TEXT_HPP
