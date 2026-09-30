#ifndef APPBOX_COMMON_FILESYSTEM_ISOLATION_HPP
#define APPBOX_COMMON_FILESYSTEM_ISOLATION_HPP

#include "IsolationDocument.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace appbox
{

/**
 * @brief Isolation mode of a file or folder of the virtual filesystem.
 *
 * The modes describe how a sandboxed process sees the entry and where its
 * modifications land. Every mode but `Merge` keeps the host filesystem
 * untouched: the writes of the sandboxed process land in the overlay of the
 * sandbox, while `Merge` lets a write reach the host filesystem by design.
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
 * - `Merge` - the host filesystem and the virtual filesystem are both visible
 *   with the virtual one taking precedence, like `WriteCopy`, but a
 *   modification is not always redirected into the sandbox: a write of an
 *   entry which the host filesystem does not hold while a sandbox layer does
 *   is redirected into the sandbox, and every other write lands in the host
 *   filesystem, which also creates an entry no layer holds at all. The host
 *   folders above a written entry are created when they are missing. A delete
 *   follows the same rule, so an entry the host filesystem holds is really
 *   removed while an entry only the sandbox holds is recorded as deleted
 *   inside the sandbox.
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
 * `WriteCopy` and `Merge` describe the merge of a **folder** with the host
 * filesystem, which a single file cannot express, so a file carries neither of
 * them: a file the user picks a mode for offers `Full` and `Whiteout` only and
 * follows the mode of the closest folder above it in every other case.
 *
 * The mode of a folder reaches the entries below it: an entry which carries no
 * mode of its own follows the closest folder above it which does, and a folder
 * which the user never touched follows `WriteCopy` while a file which the user
 * never touched follows `Full`. A path no listed entry covers at all follows
 * the entry of the **root of the view**, which is the entry whose path is
 * empty, and falls back to the default of its kind when the document holds no
 * such entry; the root entry is what decides the mode of a location outside
 * the recorded paths.
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
    Merge,     ///< Host and sandbox merged, writes prefer the host filesystem.
    Whiteout   ///< Invisible for the sandbox, creation lands in the sandbox.
};

/**
 * @brief Kind of an entry of the virtual filesystem.
 *
 * The kind decides which isolation modes an entry accepts: a folder offers
 * `Full`, `Write Copy`, `Merge` and `Whiteout`, a file offers `Full` and
 * `Whiteout` only.
 */
enum class FilesystemEntryKind
{
    File,     ///< A file of the virtual filesystem.
    Directory ///< A folder of the virtual filesystem.
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
 * the overlay of the archive, the launcher hands its path to the sandbox, and
 * the sandbox redirects the filesystem of the packaged application through the
 * modes. The packer lists the entries the user set a mode for; an entry which
 * is not listed follows the closest listed folder above it, then the root
 * entry of the view, and falls back to the default of its kind (`WriteCopy`
 * for a folder, `Full` for a file).
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
 *
 * An entry whose path is **empty** is the root of the view: it is a folder and
 * it decides the mode of every path no other entry covers, including the
 * locations which are not part of the virtual filesystem at all (for example
 * `C:\Windows` of a workspace which imports into `#ProgramFiles#` only).
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
    case FilesystemIsolation::Merge:
        return "merge";
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
    if (normalized == "merge")
    {
        out = FilesystemIsolation::Merge;
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
 * A folder accepts every mode; a file accepts `Full` and `Whiteout` only,
 * because `WriteCopy` and `Merge` describe the merge of a folder with the host
 * filesystem, which a single file cannot express.
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
    return isolation == FilesystemIsolation::Full || isolation == FilesystemIsolation::Whiteout;
}

/**
 * @brief One entry of the filesystem isolation file.
 *
 * The structure is the schema of one listed path: the packer fills it while it
 * writes the file of the workspace, and the sandbox reads the file back into
 * the same structure, so neither side parses the JSON object of an entry
 * member by member.
 */
struct Entry
{
    /**
     * @brief Path of the entry in the virtual filesystem, in UTF-8.
     *
     * The first component is the layer key of a preset directory and the
     * remaining ones are the path below it. An **empty** path is the root of
     * the view, which decides the mode of every path no other entry covers; it
     * is a folder, so its kind is `directory`.
     */
    std::string path;

    /**
     * @brief Kind of the entry the mode was picked for.
     */
    FilesystemEntryKind kind = FilesystemEntryKind::Directory;

    /**
     * @brief Isolation mode of the entry.
     */
    FilesystemIsolation isolation = FilesystemIsolation::WriteCopy;
};

/**
 * @brief The content of the filesystem isolation file.
 */
struct Document
{
    /**
     * @brief Schema version the document was written with.
     *
     * The version is a member of the file and not a rule of the schema: the
     * reader compares it with kVersion and refuses a document of another
     * version, so a file of a newer schema is never read with the rules of this
     * one.
     */
    int version = kVersion;

    /**
     * @brief The listed entries, in the order of the file.
     */
    std::vector<Entry> entries;
};

/**
 * @brief Store one entry of the filesystem isolation file.
 * @param[out] json Object which receives the entry.
 * @param[in] entry The entry to store.
 */
inline void to_json(nlohmann::json& json, const Entry& entry)
{
    json = nlohmann::json::object();
    json[kPathKey] = entry.path;
    json[kKindKey] = EntryKindToken(entry.kind);
    json[kIsolationKey] = IsolationToken(entry.isolation);
}

/**
 * @brief Read one entry of the filesystem isolation file.
 *
 * The call refuses everything the packer would never write: an entry which is
 * not an object, a member which is missing or of another type, a root entry
 * which is not a folder, an unknown kind or mode, and a mode which the kind
 * cannot hold.
 *
 * @param[in] json Object holding the entry.
 * @param[out] entry The entry to fill.
 * @throw appbox::IsolationDocumentError The entry does not fit the schema.
 */
inline void from_json(const nlohmann::json& json, Entry& entry)
{
    const std::string holder = "a filesystem isolation file entry";
    isolation_document::RequireObject(json, holder);

    entry.path = isolation_document::RequiredText(json, kPathKey, holder);

    const std::string   kind_token = isolation_document::RequiredText(json, kKindKey, holder);
    FilesystemEntryKind kind = FilesystemEntryKind::Directory;
    if (!ParseEntryKindToken(kind_token, kind))
    {
        isolation_document::Throw("unknown entry kind '" + kind_token + "' in the filesystem isolation file");
    }

    /*
     * An entry without a path is the root of the view, which is the folder
     * every path no other entry covers belongs to.
     */
    if (entry.path.empty() && kind != FilesystemEntryKind::Directory)
    {
        isolation_document::Throw("a filesystem isolation file entry without a path has to be a directory");
    }

    const std::string   isolation_token = isolation_document::RequiredText(json, kIsolationKey, holder);
    FilesystemIsolation isolation = FilesystemIsolation::Full;
    if (!ParseIsolationToken(isolation_token, isolation))
    {
        isolation_document::Throw("unknown isolation mode '" + isolation_token + "' in the filesystem isolation file");
    }

    if (!IsAllowed(isolation, kind))
    {
        isolation_document::Throw("the isolation mode '" + isolation_token + "' cannot be used for a " +
                                  EntryKindToken(kind) + " in the filesystem isolation file");
    }

    entry.kind = kind;
    entry.isolation = isolation;
}

/**
 * @brief Store the content of the filesystem isolation file.
 * @param[out] json Object which receives the document.
 * @param[in] document The document to store.
 */
inline void to_json(nlohmann::json& json, const Document& document)
{
    json = nlohmann::json::object();
    json[kVersionKey] = document.version;

    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : document.entries)
    {
        entries.push_back(nlohmann::json(entry));
    }
    json[kEntriesKey] = std::move(entries);
}

/**
 * @brief Read the content of the filesystem isolation file.
 *
 * A document which does not list any entry describes a file which sets no mode
 * at all, which is what a session without a mode writes.
 *
 * @param[in] json Object holding the document.
 * @param[out] document The document to fill.
 * @throw appbox::IsolationDocumentError The document does not fit the schema.
 */
inline void from_json(const nlohmann::json& json, Document& document)
{
    const std::string holder = "the filesystem isolation file";
    isolation_document::RequireObject(json, holder);

    document = Document{};
    document.version = isolation_document::RequiredInt(json, kVersionKey, holder);

    const auto* entries = isolation_document::OptionalArray(json, kEntriesKey, holder);
    if (entries == nullptr)
    {
        return;
    }

    for (const auto& item : *entries)
    {
        document.entries.push_back(item.get<Entry>());
    }
}

} // namespace filesystem_isolation

} // namespace appbox

#endif // APPBOX_COMMON_FILESYSTEM_ISOLATION_HPP
