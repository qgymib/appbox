#ifndef APPBOX_COMMON_ENVIRONMENT_ISOLATION_HPP
#define APPBOX_COMMON_ENVIRONMENT_ISOLATION_HPP

#include "IsolationDocument.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace appbox
{

/**
 * @brief Isolation mode of an environment variable of the sandbox.
 *
 * The modes describe what the sandboxed process sees while it reads the
 * variable. No mode ever modifies the environment of the host: the value the
 * sandbox reports is composed from the value the user entered in the packer and
 * from the value of the host.
 *
 * - `Full` - the variable of the host is invisible, the sandbox reports the
 *   value the user entered.
 * - `WriteCopy` - the value of the host is visible and is merged with the value
 *   the user entered, and the way the two are merged is picked by
 *   EnvironmentMergeMode. This is the default mode of the workspace.
 *
 * A run composes the environment of its layers in order (see
 * ComposeEnvironmentValue()), so the "value of the host" of a layer is the
 * value the layers below it composed: `Full` drops the value of the host and
 * the value of every layer below the layer which carries the mode.
 *
 * The enumeration lives in `common/` because the packer and the sandbox share
 * it: the packer stores the modes of the workspace and the sandbox applies them
 * while it composes the environment of the packaged application.
 */
enum class EnvironmentIsolation
{
    Full,     ///< Sandbox only, the value of the host is hidden.
    WriteCopy ///< Host and sandbox merged, the merge is picked by the merge mode.
};

/**
 * @brief Merge mode of an environment variable of the sandbox.
 *
 * The mode decides how the value the user entered and the value of the host are
 * composed while the isolation mode is `WriteCopy`. `Replace` and `Host` pick
 * one of the two values, `Prepend` and `Append` join them with the merge string
 * of the entry.
 *
 * - `Replace` - the sandbox reports the value the user entered. With a host
 *   value of `foo` and a stored value of `bar` the sandbox reports `bar`.
 * - `Host` - the sandbox reports the value of the host, the stored value is
 *   ignored.
 * - `Prepend` - the stored value is joined in front of the value of the host
 *   with the merge string: `bar` and `;` in front of `foo` report `bar;foo`.
 * - `Append` - the stored value is joined behind the value of the host with the
 *   merge string: `bar` and `;` behind `foo` report `foo;bar`.
 */
enum class EnvironmentMergeMode
{
    Replace, ///< The stored value replaces the value of the host.
    Host,    ///< The value of the host is used, the stored value is ignored.
    Prepend, ///< The stored value is joined in front of the value of the host.
    Append   ///< The stored value is joined behind the value of the host.
};

/**
 * @brief Vocabulary of the environment isolation.
 *
 * The tokens are the text form of the modes, which the project file stores and
 * reads. They are ASCII, so they are handled as narrow text like the JSON
 * document which carries them.
 */
namespace environment_isolation
{

/**
 * @brief Schema of the environment isolation file.
 *
 * The file is the JSON document which carries the environment configuration
 * from the packer to the sandbox. The packer writes it into the environment
 * domain of the archive as `app/environment/isolation.json`, the launcher hands
 * its path to the sandbox, and the sandbox composes the environment of the
 * packaged application from its entries.
 *
 * ```
 * {
 *   "version": 1,
 *   "entries": [ { "name": "PATH", "value": "C:\\MyApp\\bin",
 *                  "isolation": "write_copy", "merge": "prepend",
 *                  "merge_string": ";" } ]
 * }
 * ```
 *
 * Every entry needs all five of its members; the isolation and the merge mode
 * are the tokens of IsolationToken() and MergeModeToken(), which are read
 * ignoring the case and with spaces or dashes in place of the underscore of
 * the canonical token. An entry which is listed twice and an entry without a
 * name are refused while the file is read, because the environment of a
 * process holds a variable once and names it with a non empty name.
 */

/**
 * @brief Version of the environment isolation file written by the packer.
 *
 * A file of a different version is rejected instead of being interpreted with
 * the rules of another schema.
 */
inline constexpr int kVersion = 1;

/** Member name of the schema version. */
inline constexpr const char* kVersionKey = "version";

/** Member name of the entry list. */
inline constexpr const char* kEntriesKey = "entries";

/** Member name of the variable of an entry. */
inline constexpr const char* kNameKey = "name";

/** Member name of the value of an entry. */
inline constexpr const char* kValueKey = "value";

/** Member name of the isolation mode of an entry. */
inline constexpr const char* kIsolationKey = "isolation";

/** Member name of the merge mode of an entry. */
inline constexpr const char* kMergeKey = "merge";

/** Member name of the merge string of an entry. */
inline constexpr const char* kMergeStringKey = "merge_string";

/**
 * @brief Schema of the environment state file of a sandbox.
 *
 * The file is the JSON document which carries the modifications the packaged
 * application made to its environment. The sandbox writes it into the state
 * directory of the sandbox as `data/environment/state.json` while the
 * application runs, and it reads the file while it composes the environment of
 * the next run, so a modification survives the end of the process which made
 * it. Deleting the state directory therefore resets the environment of the
 * sandbox to the state the archive was packed with.
 *
 * ```
 * {
 *   "version": 1,
 *   "entries": [ { "name": "APPBOX_MODE", "value": "changed", "deleted": false },
 *                { "name": "APPBOX_OLD", "value": "", "deleted": true } ]
 * }
 * ```
 *
 * The entries are the modifications in the order the application made them:
 * an entry replaces the value of a variable and an entry with `deleted` set
 * removes it. The document holds the modifications and not the environment of
 * the sandbox, so a variable the application never touched keeps the value of
 * the host at the time of the run instead of being frozen into the file.
 */

/**
 * @brief Version of the environment state file written by the sandbox.
 */
inline constexpr int kStateVersion = 1;

/** Member name of the state schema version. */
inline constexpr const char* kStateVersionKey = "version";

/** Member name of the state entry list. */
inline constexpr const char* kStateEntriesKey = "entries";

/** Member name of the variable of a state entry. */
inline constexpr const char* kStateNameKey = "name";

/** Member name of the value of a state entry. */
inline constexpr const char* kStateValueKey = "value";

/**
 * @brief Member name of the flag which removes a variable.
 *
 * The flag is written for every entry, so a reader never has to guess what a
 * missing member means.
 */
inline constexpr const char* kStateDeletedKey = "deleted";

/**
 * @brief Name of the variable which holds the list of the search paths.
 *
 * The variable is the one the workspace fills in on its own: the sandbox
 * prepends the paths of the packaged application to the search path of the
 * host, so the value of the variable is merged instead of being replaced.
 */
inline constexpr const wchar_t* kPathVariableName = L"PATH";

/**
 * @brief Merge string the workspace fills in for the search path variable.
 *
 * A search path is a list of paths which are separated by a semicolon, so the
 * value of the variable has to be joined with that separator.
 */
inline constexpr const wchar_t* kPathVariableMergeString = L";";

/**
 * @brief Merge mode the workspace fills in for the search path variable.
 *
 * The paths of the packaged application are searched before the paths of the
 * host, so the value of the variable is prepended.
 */
inline constexpr EnvironmentMergeMode kPathVariableMergeMode = EnvironmentMergeMode::Prepend;

/**
 * @brief Merge mode a variable which is not the search path starts with.
 *
 * A variable the user adds carries the value it was given and replaces the
 * value of the host, which is what a variable of a packaged application
 * normally does.
 */
inline constexpr EnvironmentMergeMode kDefaultMergeMode = EnvironmentMergeMode::Replace;

/**
 * @brief Whether a name is the search path variable.
 *
 * The comparison ignores the case, because the environment of a process is
 * case insensitive on Windows and `Path` therefore names the same variable as
 * `PATH` does. The name is ASCII, so it is compared character by character.
 *
 * @param[in] name Name of an environment variable.
 * @return true when the name is the search path variable.
 */
inline bool IsPathVariableName(std::wstring_view name)
{
    if (name.size() != 4)
    {
        return false;
    }

    constexpr wchar_t kExpected[] = L"path";
    for (std::size_t index = 0; index < 4; ++index)
    {
        wchar_t character = name[index];
        if (character >= L'A' && character <= L'Z')
        {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
        if (character != kExpected[index])
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Get the token of an isolation mode.
 * @param[in] isolation The isolation mode.
 * @return The canonical lower case token, for example `"write_copy"`.
 */
inline const char* IsolationToken(EnvironmentIsolation isolation)
{
    switch (isolation)
    {
    case EnvironmentIsolation::Full:
        return "full";
    case EnvironmentIsolation::WriteCopy:
        return "write_copy";
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
inline bool ParseIsolationToken(std::string_view token, EnvironmentIsolation& out)
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
        out = EnvironmentIsolation::Full;
        return true;
    }
    if (normalized == "write_copy" || normalized == "writecopy")
    {
        out = EnvironmentIsolation::WriteCopy;
        return true;
    }
    return false;
}

/**
 * @brief Get the token of a merge mode.
 * @param[in] merge The merge mode.
 * @return The canonical lower case token, for example `"prepend"`.
 */
inline const char* MergeModeToken(EnvironmentMergeMode merge)
{
    switch (merge)
    {
    case EnvironmentMergeMode::Replace:
        return "replace";
    case EnvironmentMergeMode::Host:
        return "host";
    case EnvironmentMergeMode::Prepend:
        return "prepend";
    case EnvironmentMergeMode::Append:
        return "append";
    }
    return "replace";
}

/**
 * @brief Resolve a merge mode from its token.
 *
 * The comparison ignores the case and accepts spaces and dashes in place of
 * the underscore of a canonical token, like the token of an isolation mode
 * does.
 *
 * @param[in] token The token to resolve.
 * @param[out] out The resolved mode when the token is known.
 * @return true when the token names a merge mode.
 */
inline bool ParseMergeModeToken(std::string_view token, EnvironmentMergeMode& out)
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

    if (normalized == "replace")
    {
        out = EnvironmentMergeMode::Replace;
        return true;
    }
    if (normalized == "host")
    {
        out = EnvironmentMergeMode::Host;
        return true;
    }
    if (normalized == "prepend")
    {
        out = EnvironmentMergeMode::Prepend;
        return true;
    }
    if (normalized == "append")
    {
        out = EnvironmentMergeMode::Append;
        return true;
    }
    return false;
}

/**
 * @brief Value the sandbox reports for one configured variable.
 *
 * The value is the whole answer of the composition: a variable which is not
 * visible at all is absent from the environment of the sandbox, which is not
 * the same as a variable which carries an empty value.
 */
struct ComposedValue
{
    /**
     * @brief Whether the variable is part of the environment of the sandbox.
     *
     * The flag is false while the merge mode `Host` composes a variable the
     * host does not hold: the stored value is ignored and there is no host
     * value to report either, so the variable stays invisible.
     */
    bool visible = true;

    /**
     * @brief Value the sandbox reports while the variable is visible.
     */
    std::wstring value;
};

/**
 * @brief Whether the value below a layer is part of the answer of a mode.
 *
 * Only `Write Copy` lets the packaged application see the value which was
 * composed below the layer; `Full` hides it and reports the stored value
 * alone, so a layer whose isolation mode is `Full` drops the value of the host
 * and the value of every layer below it.
 *
 * @param[in] isolation The isolation mode.
 * @return true when the value below the layer is visible.
 */
inline bool HostValueIsVisible(EnvironmentIsolation isolation)
{
    return isolation == EnvironmentIsolation::WriteCopy;
}

/**
 * @brief Compose the value the sandbox reports for one variable.
 *
 * The rules are the ones of the workspace of the packer. The value of the host
 * is the value below the first layer of a run, and the value a layer composed
 * is the value below the layer which follows it, so the same rules describe a
 * single layer and a chain of layers:
 *
 * - `Full` reports the stored value and ignores the merge mode, because the
 *   value below the layer is invisible.
 * - `Write Copy` reports the stored value for `Replace`, the value below the
 *   layer for `Host`, and the two values joined by the merge string for
 *   `Prepend` and `Append`.
 * - A value below the layer which is absent contributes nothing, so `Prepend`
 *   and `Append` report the stored value alone: the merge string joins two
 *   values and is therefore not written while there is only one. The same
 *   holds for a value which is present but empty, because an empty entry of a
 *   list like the search path names the current directory and a stray
 *   separator would add one.
 * - `Host` with a value below the layer which is absent reports no variable at
 *   all: the stored value is ignored by the mode, so there is nothing to
 *   report.
 *
 * A chain of layers therefore composes from the value of the host outwards: a
 * host `PATH` of `vx`, a layer which prepends `v0` and a layer which appends
 * `v1` report `v0;vx;v1`.
 *
 * @param[in] host_present Whether the layers below the layer hold the variable.
 * @param[in] host_value Value below the layer, empty while it is absent.
 * @param[in] value Value the user entered in the workspace.
 * @param[in] isolation Isolation mode of the variable.
 * @param[in] merge Merge mode of the variable.
 * @param[in] merge_string Text which joins the two values.
 * @return The value the sandbox reports, see ComposedValue.
 */
inline ComposedValue ComposeEnvironmentValue(bool host_present, std::wstring_view host_value, std::wstring_view value,
                                             EnvironmentIsolation isolation, EnvironmentMergeMode merge,
                                             std::wstring_view merge_string)
{
    if (!HostValueIsVisible(isolation))
    {
        return ComposedValue{ true, std::wstring(value) };
    }

    switch (merge)
    {
    case EnvironmentMergeMode::Replace:
        return ComposedValue{ true, std::wstring(value) };
    case EnvironmentMergeMode::Host:
        if (!host_present)
        {
            return ComposedValue{ false, std::wstring() };
        }
        return ComposedValue{ true, std::wstring(host_value) };
    case EnvironmentMergeMode::Prepend:
        if (!host_present || host_value.empty())
        {
            return ComposedValue{ true, std::wstring(value) };
        }
        return ComposedValue{ true, std::wstring(value) + std::wstring(merge_string) + std::wstring(host_value) };
    case EnvironmentMergeMode::Append:
        if (!host_present || host_value.empty())
        {
            return ComposedValue{ true, std::wstring(value) };
        }
        return ComposedValue{ true, std::wstring(host_value) + std::wstring(merge_string) + std::wstring(value) };
    }

    return ComposedValue{ true, std::wstring(value) };
}

/**
 * @brief Build the prefix of an error description which names an entry.
 *
 * The readers of the environment documents report the entry which was refused
 * with its position in the list, like the project file does for its lists.
 *
 * @param[in] index Position of the entry.
 * @return The prefix, for example `"entries[0]: "`.
 */
inline std::string EntryPrefix(std::size_t index)
{
    return "entries[" + std::to_string(index) + "]: ";
}

/**
 * @brief Read a required member of an entry which holds a text.
 * @param[in] entry Object of the entry.
 * @param[in] key Name of the member.
 * @param[in] prefix Prefix of the error description, names the entry.
 * @return The text of the member.
 * @throw appbox::IsolationDocumentError The member is missing or not a text.
 */
inline std::string EntryText(const nlohmann::json& entry, const char* key, const std::string& prefix)
{
    const auto* member = isolation_document::FindMember(entry, key);
    if (member == nullptr || !member->is_string())
    {
        isolation_document::Throw(prefix + "the member '" + key + "' is missing or is not a string");
    }

    return member->get<std::string>();
}

/**
 * @brief Read a required member of an entry which holds a flag.
 * @param[in] entry Object of the entry.
 * @param[in] key Name of the member.
 * @param[in] prefix Prefix of the error description, names the entry.
 * @return The value of the member.
 * @throw appbox::IsolationDocumentError The member is missing or not a flag.
 */
inline bool EntryFlag(const nlohmann::json& entry, const char* key, const std::string& prefix)
{
    const auto* member = isolation_document::FindMember(entry, key);
    if (member == nullptr || !member->is_boolean())
    {
        isolation_document::Throw(prefix + "the member '" + key + "' is missing or is not a boolean");
    }

    return member->get<bool>();
}

/**
 * @brief Read a required member of an entry which holds the token of a mode.
 * @param[in] entry Object of the entry.
 * @param[in] key Name of the member.
 * @param[in] prefix Prefix of the error description, names the entry.
 * @param[in] parse Resolver of the token.
 * @return The mode of the member.
 * @throw appbox::IsolationDocumentError The member is missing or names no mode.
 */
template <typename Mode, typename ParseFn>
inline Mode EntryMode(const nlohmann::json& entry, const char* key, const std::string& prefix, ParseFn parse)
{
    const std::string token = EntryText(entry, key, prefix);

    Mode mode{};
    if (!parse(token, mode))
    {
        isolation_document::Throw(prefix + "the member '" + key + "' is not a known mode: '" + token + "'");
    }

    return mode;
}

/**
 * @brief Get the entry list of an environment document.
 * @param[in] json Object of the document.
 * @param[in] key Name of the entry list member.
 * @param[in] description Description of the document, used by the error text.
 * @return The entry list.
 * @throw appbox::IsolationDocumentError The document carries no entry list.
 */
inline const nlohmann::json& EntryList(const nlohmann::json& json, const char* key, const std::string& description)
{
    const auto* entries = isolation_document::FindMember(json, key);
    if (entries == nullptr || !entries->is_array())
    {
        isolation_document::Throw(description + " carries no entry list");
    }

    return *entries;
}

/**
 * @brief Reject an entry which is not a JSON object.
 * @param[in] entry The entry to check.
 * @param[in] prefix Prefix of the error description, names the entry.
 * @throw appbox::IsolationDocumentError The entry is not an object.
 */
inline void RequireEntryObject(const nlohmann::json& entry, const std::string& prefix)
{
    if (!entry.is_object())
    {
        isolation_document::Throw(prefix + "the entry is not an object");
    }
}

/**
 * @brief One variable of the environment isolation file.
 *
 * The structure is the schema of one listed variable: the packer fills it while
 * it writes the file of the workspace, and the sandbox reads the file back into
 * the same structure, so neither side parses the JSON object of an entry member
 * by member.
 *
 * A name which is listed twice is refused by the reader of the file and not by
 * the structure: the comparison of two names is the case insensitive ordinal
 * comparison of the operating system, which is applied to the wide text of the
 * names while the entries are applied.
 */
struct Entry
{
    /**
     * @brief Name of the variable, in UTF-8.
     */
    std::string name;

    /**
     * @brief Value the user entered, in UTF-8, may be empty.
     */
    std::string value;

    /**
     * @brief Isolation mode of the variable.
     */
    EnvironmentIsolation isolation = EnvironmentIsolation::WriteCopy;

    /**
     * @brief Merge mode of the variable.
     */
    EnvironmentMergeMode merge = kDefaultMergeMode;

    /**
     * @brief Text which joins the two values, in UTF-8.
     */
    std::string merge_string;
};

/**
 * @brief The content of the environment isolation file.
 */
struct Document
{
    /**
     * @brief Schema version the document was written with.
     *
     * The reader compares the version with kVersion and refuses a document of
     * another version, so a file of a newer schema is never read with the rules
     * of this one.
     */
    int version = kVersion;

    /**
     * @brief The listed variables, in the order of the file.
     */
    std::vector<Entry> entries;
};

/**
 * @brief One modification of the environment state file.
 *
 * The structure is the schema of one listed modification: the sandbox writes
 * the document while the packaged application changes its environment and reads
 * it back while it composes the environment of the next run.
 */
struct StateEntry
{
    /**
     * @brief Name of the variable, in UTF-8.
     */
    std::string name;

    /**
     * @brief Value the application stored, in UTF-8, empty for a removal.
     */
    std::string value;

    /**
     * @brief Whether the application removed the variable.
     */
    bool deleted = false;
};

/**
 * @brief The content of the environment state file.
 */
struct StateDocument
{
    /**
     * @brief Schema version the document was written with.
     *
     * The reader compares the version with kStateVersion and refuses a document
     * of another version.
     */
    int version = kStateVersion;

    /**
     * @brief The modifications, in the order the application made them.
     */
    std::vector<StateEntry> entries;
};

/**
 * @brief Store one variable of the environment isolation file.
 * @param[out] json Object which receives the entry.
 * @param[in] entry The entry to store.
 */
inline void to_json(nlohmann::json& json, const Entry& entry)
{
    json = nlohmann::json::object();
    json[kNameKey] = entry.name;
    json[kValueKey] = entry.value;
    json[kIsolationKey] = IsolationToken(entry.isolation);
    json[kMergeKey] = MergeModeToken(entry.merge);
    json[kMergeStringKey] = entry.merge_string;
}

/**
 * @brief Read one variable of the environment isolation file.
 *
 * An entry without a name and an entry whose name carries an equals sign are
 * refused, because the environment of a process holds a variable once and names
 * it with a non empty name which the equals sign separates from its value.
 *
 * @param[in] json Object holding the entry.
 * @param[in] index Position of the entry, which the error text names.
 * @param[out] entry The entry to fill.
 * @throw appbox::IsolationDocumentError The entry does not fit the schema.
 */
inline void ReadEntry(const nlohmann::json& json, std::size_t index, Entry& entry)
{
    const std::string prefix = EntryPrefix(index);
    RequireEntryObject(json, prefix);

    entry = Entry{};
    entry.name = EntryText(json, kNameKey, prefix);
    entry.value = EntryText(json, kValueKey, prefix);
    entry.merge_string = EntryText(json, kMergeStringKey, prefix);
    entry.isolation = EntryMode<EnvironmentIsolation>(json, kIsolationKey, prefix, ParseIsolationToken);
    entry.merge = EntryMode<EnvironmentMergeMode>(json, kMergeKey, prefix, ParseMergeModeToken);

    if (entry.name.empty())
    {
        isolation_document::Throw(prefix + "the variable carries no name");
    }
    if (entry.name.find('=') != std::string::npos)
    {
        isolation_document::Throw(prefix + "the name of the variable carries an equals sign");
    }
}

/**
 * @brief Store the content of the environment isolation file.
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
 * @brief Read the content of the environment isolation file.
 * @param[in] json Object holding the document.
 * @param[out] document The document to fill.
 * @throw appbox::IsolationDocumentError The document does not fit the schema.
 */
inline void from_json(const nlohmann::json& json, Document& document)
{
    const std::string description = "the environment isolation file";
    isolation_document::RequireObject(json, description);

    document = Document{};
    document.version = isolation_document::RequiredInt(json, kVersionKey, description);

    const nlohmann::json& entries = EntryList(json, kEntriesKey, description);
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        Entry entry;
        ReadEntry(entries[index], index, entry);
        document.entries.push_back(std::move(entry));
    }
}

/**
 * @brief Store one modification of the environment state file.
 * @param[out] json Object which receives the entry.
 * @param[in] entry The entry to store.
 */
inline void to_json(nlohmann::json& json, const StateEntry& entry)
{
    json = nlohmann::json::object();
    json[kStateNameKey] = entry.name;
    json[kStateValueKey] = entry.value;
    json[kStateDeletedKey] = entry.deleted;
}

/**
 * @brief Read one modification of the environment state file.
 * @param[in] json Object holding the entry.
 * @param[in] index Position of the entry, which the error text names.
 * @param[out] entry The entry to fill.
 * @throw appbox::IsolationDocumentError The entry does not fit the schema.
 */
inline void ReadEntry(const nlohmann::json& json, std::size_t index, StateEntry& entry)
{
    const std::string prefix = EntryPrefix(index);
    RequireEntryObject(json, prefix);

    entry = StateEntry{};
    entry.name = EntryText(json, kStateNameKey, prefix);
    entry.value = EntryText(json, kStateValueKey, prefix);
    entry.deleted = EntryFlag(json, kStateDeletedKey, prefix);

    if (entry.name.empty())
    {
        isolation_document::Throw(prefix + "the variable carries no name");
    }
}

/**
 * @brief Store the content of the environment state file.
 * @param[out] json Object which receives the document.
 * @param[in] document The document to store.
 */
inline void to_json(nlohmann::json& json, const StateDocument& document)
{
    json = nlohmann::json::object();
    json[kStateVersionKey] = document.version;

    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : document.entries)
    {
        entries.push_back(nlohmann::json(entry));
    }
    json[kStateEntriesKey] = std::move(entries);
}

/**
 * @brief Read the content of the environment state file.
 * @param[in] json Object holding the document.
 * @param[out] document The document to fill.
 * @throw appbox::IsolationDocumentError The document does not fit the schema.
 */
inline void from_json(const nlohmann::json& json, StateDocument& document)
{
    const std::string description = "the environment state file";
    isolation_document::RequireObject(json, description);

    document = StateDocument{};
    document.version = isolation_document::RequiredInt(json, kStateVersionKey, description);

    const nlohmann::json& entries = EntryList(json, kStateEntriesKey, description);
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        StateEntry entry;
        ReadEntry(entries[index], index, entry);
        document.entries.push_back(std::move(entry));
    }
}

} // namespace environment_isolation

} // namespace appbox

#endif // APPBOX_COMMON_ENVIRONMENT_ISOLATION_HPP
