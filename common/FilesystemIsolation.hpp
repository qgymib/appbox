#ifndef APPBOX_COMMON_FILESYSTEM_ISOLATION_HPP
#define APPBOX_COMMON_FILESYSTEM_ISOLATION_HPP

#include <string>
#include <string_view>

namespace appbox
{

/**
 * @brief Isolation mode of a file or folder of the virtual filesystem.
 *
 * The modes describe how a sandboxed process sees the entry. No mode ever
 * modifies the host filesystem: every write of the sandboxed process lands in
 * the overlay of the sandbox.
 *
 * For a **folder** the modes are:
 *
 * - `Full` - only the virtual filesystem is visible, every access is
 *   redirected into the overlay even when the host holds the folder.
 *   Modifications of the folder attributes and of the files below it are
 *   redirected into the sandbox.
 * - `WriteCopy` - the host filesystem and the virtual filesystem are both
 *   visible with the virtual one taking precedence, and every modification is
 *   redirected into the sandbox. This is the default mode of a folder.
 * - `Whiteout` - the folder is invisible for the sandboxed process: opening,
 *   reading and writing report `File Not Found`, even when the host holds the
 *   folder. Creating the folder succeeds inside the sandbox, and the folder is
 *   readable and writable afterwards.
 *
 * For a **file** only two modes exist:
 *
 * - `Full` - every write of the file lands in the sandbox. This is the default
 *   mode of a file.
 * - `Whiteout` - the file is invisible for the sandboxed process: opening,
 *   reading and writing report `File Not Found`, even when the host holds the
 *   file. Creating the file succeeds inside the sandbox, and the file is
 *   readable and writable afterwards.
 *
 * The mode of a folder reaches the entries below it: an entry which carries no
 * mode of its own follows the closest folder above it which does, and a folder
 * which the user never touched follows `WriteCopy` while a file which the user
 * never touched follows `Full`.
 *
 * The enumeration lives in `common/` because the packer and the sandbox share
 * it: the packer stores the modes of the workspace and writes them into the
 * isolation file of the archive (see the schema below), and the sandbox reads
 * them back and redirects the filesystem of the packaged application through
 * them.
 */
enum class FilesystemIsolation
{
    Full,      ///< Sandbox only, every modification lands in the overlay.
    WriteCopy, ///< Host and sandbox with sandbox precedence, writes copied up.
    Whiteout   ///< Invisible for the sandbox, creation lands in the sandbox.
};

/**
 * @brief Kind of an entry of the virtual filesystem.
 *
 * The kind decides which isolation modes an entry accepts: a folder offers
 * `Full`, `Write Copy` and `Whiteout`, a file offers `Full` and `Whiteout`
 * only.
 */
enum class FilesystemEntryKind
{
    File,      ///< A file of the virtual filesystem.
    Directory  ///< A folder of the virtual filesystem.
};

/**
 * @brief Tokens of the filesystem isolation modes and entry kinds.
 *
 * The tokens are the text form of the modes and of the entry kinds, which the
 * project file stores and reads. They are ASCII, so they are handled as narrow
 * text like the JSON documents which carry them.
 */
namespace filesystem_isolation
{

/**
 * @brief Schema of the filesystem isolation file.
 *
 * The file is the JSON document which carries the isolation modes of the
 * virtual filesystem from the packer to the sandbox: the packer writes it into
 * the overlay of the archive, the loader hands its path to the sandbox, and
 * the sandbox redirects the filesystem of the packaged application through the
 * modes. The packer lists the entries the user set a mode for; an entry which
 * is not listed follows the closest listed folder above it and falls back to
 * the default of its kind (`WriteCopy` for a folder, `Full` for a file).
 *
 * ```
 * {
 *   "version": 1,
 *   "entries": [ { "path": "#ProgramFiles#\\MyApp", "kind": "directory",
 *                  "isolation": "full" } ]
 * }
 * ```
 *
 * The path of an entry is a path of the virtual filesystem, which is the path
 * the `Source Path` column of the workspace shows: the first component is the
 * layer key of a preset directory and the remaining ones are the path below
 * it. The sandbox translates the layer key back into the folder the layer is
 * mapped to before it looks the mode up.
 */

/**
 * @brief Version of the isolation file written by the packer.
 *
 * A file of a different version is rejected instead of being interpreted with
 * the rules of another schema.
 */
inline constexpr int kVersion = 1;

/** Member name of the schema version. */
inline constexpr const char* kVersionKey = "version";

/** Member name of the entry list. */
inline constexpr const char* kEntriesKey = "entries";

/** Member name of the virtual path of an entry. */
inline constexpr const char* kPathKey = "path";

/** Member name of the entry kind of an entry. */
inline constexpr const char* kKindKey = "kind";

/** Member name of the isolation mode of an entry. */
inline constexpr const char* kIsolationKey = "isolation";

/**
 * @brief Get the token of an isolation mode.
 * @param[in] isolation The isolation mode.
 * @return The canonical lower case token, for example `"write_copy"`.
 */
inline const char* IsolationToken(FilesystemIsolation isolation)
{
    switch (isolation)
    {
    case FilesystemIsolation::Full:
        return "full";
    case FilesystemIsolation::WriteCopy:
        return "write_copy";
    case FilesystemIsolation::Whiteout:
        return "whiteout";
    }
    return "write_copy";
}

/**
 * @brief Resolve an isolation mode from its token.
 *
 * The comparison ignores the case and accepts spaces and dashes in place of
 * the underscore of the canonical token, so `"Write Copy"` and `"write-copy"`
 * resolve as well.
 *
 * @param[in] token The token to resolve.
 * @param[out] out The resolved mode when the token is known.
 * @return true when the token names an isolation mode.
 */
inline bool ParseIsolationToken(std::string_view token, FilesystemIsolation& out)
{
    std::string normalized;
    normalized.reserve(token.size());
    for (const char character : token)
    {
        char lower = character;
        if (lower >= 'A' && lower <= 'Z')
        {
            lower = static_cast<char>(lower - 'A' + 'a');
        }
        if (lower == ' ' || lower == '-')
        {
            lower = '_';
        }
        normalized.push_back(lower);
    }

    if (normalized == "full")
    {
        out = FilesystemIsolation::Full;
        return true;
    }
    if (normalized == "write_copy" || normalized == "writecopy")
    {
        out = FilesystemIsolation::WriteCopy;
        return true;
    }
    if (normalized == "whiteout")
    {
        out = FilesystemIsolation::Whiteout;
        return true;
    }
    return false;
}

/**
 * @brief Get the token of an entry kind.
 * @param[in] kind The entry kind.
 * @return The canonical lower case token, either `"file"` or `"directory"`.
 */
inline const char* EntryKindToken(FilesystemEntryKind kind)
{
    switch (kind)
    {
    case FilesystemEntryKind::File:
        return "file";
    case FilesystemEntryKind::Directory:
        return "directory";
    }
    return "directory";
}

/**
 * @brief Resolve an entry kind from its token.
 *
 * The comparison ignores the case and accepts `"folder"` and `"dir"` as
 * synonyms of the canonical `"directory"` token.
 *
 * @param[in] token The token to resolve.
 * @param[out] out The resolved kind when the token is known.
 * @return true when the token names an entry kind.
 */
inline bool ParseEntryKindToken(std::string_view token, FilesystemEntryKind& out)
{
    std::string normalized;
    normalized.reserve(token.size());
    for (const char character : token)
    {
        char lower = character;
        if (lower >= 'A' && lower <= 'Z')
        {
            lower = static_cast<char>(lower - 'A' + 'a');
        }
        normalized.push_back(lower);
    }

    if (normalized == "file")
    {
        out = FilesystemEntryKind::File;
        return true;
    }
    if (normalized == "directory" || normalized == "folder" || normalized == "dir")
    {
        out = FilesystemEntryKind::Directory;
        return true;
    }
    return false;
}

/**
 * @brief Whether an isolation mode may be used for an entry kind.
 *
 * A folder accepts `Full`, `WriteCopy` and `Whiteout`; a file accepts `Full`
 * and `Whiteout` only, because `WriteCopy` describes the merge of a folder
 * with the host filesystem, which a single file cannot express.
 *
 * @param[in] isolation The isolation mode.
 * @param[in] kind The kind of the entry.
 * @return true when the mode is usable for the kind.
 */
inline bool IsAllowed(FilesystemIsolation isolation, FilesystemEntryKind kind)
{
    if (kind == FilesystemEntryKind::Directory)
    {
        return true;
    }
    return isolation != FilesystemIsolation::WriteCopy;
}

} // namespace filesystem_isolation

} // namespace appbox

#endif // APPBOX_COMMON_FILESYSTEM_ISOLATION_HPP
