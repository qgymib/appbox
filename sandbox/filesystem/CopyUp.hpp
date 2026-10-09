#ifndef APPBOX_SANDBOX_FILESYSTEM_COPYUP_HPP
#define APPBOX_SANDBOX_FILESYSTEM_COPYUP_HPP

#include "Resolve.hpp"
#include <string>

namespace appbox
{
namespace filesystem
{

/**
 * @brief Copy the file which carries an alternate data stream into the overlay.
 *
 * A stream cannot exist without the file which carries it, so a modification of
 * a stream is a modification of that file as well: the overlay has to hold the
 * file before the stream lands in it. The file system creates the file on its
 * own when the overlay holds no such file, and that copy would carry nothing
 * but the stream, which would shadow the content the view reports for the file.
 * The file of the view is therefore copied into the overlay first.
 *
 * A file the overlay already holds is kept as it is: it carries the
 * modifications the sandboxed process made to it. A file the view does not hold
 * is not copied either, because the file system creates it for the stream,
 * which is what makes the create of a stream of a hidden entry land in the
 * overlay.
 *
 * A path which names no stream has no such file, so the call does nothing.
 *
 * @param[in] view_path Path of the view of the entry, with or without a stream.
 * @return true when the overlay holds the file of the stream afterwards or when
 *         the view holds no such file, false when the copy failed.
 */
bool CopyUpStreamFile(const std::wstring& view_path);

/**
 * @brief Copy the entry a resolve selected into the overlay.
 *
 * The entry is copied when a layer below the overlay holds it and the overlay
 * does not: the copy is what makes a modification of an entry which only a
 * read-only layer holds land in the sandbox. The copy carries the content of
 * the entry alone, see the known gaps of the filesystem isolation.
 *
 * A path which names an alternate data stream carries the file of the stream as
 * well, see CopyUpStreamFile(): a stream is copied into the overlay together
 * with the file which holds it.
 *
 * @param[in] view_path Path of the view of the entry.
 * @param[in] resolve Resolve of that path, which may hold no layer.
 * @return true when the entry is in the overlay afterwards.
 */
bool CopyUpEntry(const std::wstring& view_path, const ResolveResult& resolve);

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_SANDBOX_FILESYSTEM_COPYUP_HPP
