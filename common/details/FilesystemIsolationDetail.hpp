#ifndef APPBOX_COMMON_DETAILS_FILESYSTEM_ISOLATION_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_FILESYSTEM_ISOLATION_DETAIL_HPP

/*
 * Implementation of `common/FilesystemIsolation.hpp`: the token parsing of the
 * modes and entry kinds, the JSON members of the schema and the noexcept reader
 * of a document. The header is included by the public header and is not meant to
 * be included on its own.
 */

#include "FilesystemIsolation.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <utility>
#include <vector>

namespace appbox
{
namespace filesystem_isolation
{

/* Definition of the contract on the declaration. */
inline const char* IsolationToken(FilesystemIsolation isolation) noexcept
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
inline tl::expected<FilesystemIsolation, ErrorCode> ParseIsolationToken(std::string_view token) noexcept
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
        return FilesystemIsolation::Full;
    }
    if (normalized == "write_copy" || normalized == "writecopy")
    {
        return FilesystemIsolation::WriteCopy;
    }
    if (normalized == "merge")
    {
        return FilesystemIsolation::Merge;
    }
    if (normalized == "whiteout")
    {
        return FilesystemIsolation::Whiteout;
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kIsolationTokenUnknown);
}

/* Definition of the contract on the declaration. */
inline const char* EntryKindToken(FilesystemEntryKind kind) noexcept
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
inline tl::expected<FilesystemEntryKind, ErrorCode> ParseEntryKindToken(std::string_view token) noexcept
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
        return FilesystemEntryKind::File;
    }
    if (normalized == "directory" || normalized == "folder" || normalized == "dir")
    {
        return FilesystemEntryKind::Directory;
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kEntryKindTokenUnknown);
}

/* Definition of the contract on the declaration. */
inline bool IsAllowed(FilesystemIsolation isolation, FilesystemEntryKind kind) noexcept
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
    if (const auto parsed = ParseEntryKindToken(kind_token))
    {
        kind = *parsed;
    }
    else
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
    if (const auto parsed = ParseIsolationToken(isolation_token))
    {
        isolation = *parsed;
    }
    else
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

/* Definition of the contract on the declaration. */
inline tl::expected<Document, ErrorCode> ParseDocument(std::string_view text) noexcept
{
    /*
     * The text is read in two steps: the JSON layer runs with the exception
     * reporting switched off, so a text which is no JSON document is answered by
     * a discarded value instead of an exception, and the member layer is the
     * `from_json()` of the schema, which reports a refused member through
     * `IsolationDocumentError`. Both are turned into the code of the table here,
     * so the reader of a file never sees an exception and never receives a text.
     */
    const auto parsed = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }

    const auto* version = isolation_document::FindMember(parsed, kVersionKey);
    if (version == nullptr || !version->is_number_integer())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }
    if (version->get<int>() != kVersion)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentUnsupportedVersion);
    }

    try
    {
        return parsed.get<Document>();
    }
    catch (const std::exception&)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }
}

} // namespace filesystem_isolation
} // namespace appbox

#endif
