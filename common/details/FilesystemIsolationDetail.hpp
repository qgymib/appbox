#ifndef APPBOX_COMMON_DETAILS_FILESYSTEM_ISOLATION_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_FILESYSTEM_ISOLATION_DETAIL_HPP

/*
 * Implementation of `common/FilesystemIsolation.hpp`: the token parsing of the
 * modes and entry kinds and the JSON members of the schema. The header is
 * included by the public header and is not meant to be included on its own.
 */

#include "FilesystemIsolation.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace appbox
{
namespace filesystem_isolation
{

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
inline bool IsAllowed(FilesystemIsolation isolation, FilesystemEntryKind kind)
{
    if (kind == FilesystemEntryKind::Directory)
    {
        return true;
    }
    return isolation == FilesystemIsolation::Full || isolation == FilesystemIsolation::Whiteout;
}

/* Definition of the contract on the declaration. */
inline void to_json(nlohmann::json& json, const Entry& entry)
{
    json = nlohmann::json::object();
    json[kPathKey] = entry.path;
    json[kKindKey] = EntryKindToken(entry.kind);
    json[kIsolationKey] = IsolationToken(entry.isolation);
}

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

#endif
