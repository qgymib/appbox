#ifndef APPBOX_COMMON_DETAILS_ENVIRONMENT_ISOLATION_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_ENVIRONMENT_ISOLATION_DETAIL_HPP

/*
 * Implementation of `common/EnvironmentIsolation.hpp`: the token parsing, the
 * composition of a value, the JSON members of the isolation and the state schema
 * and the noexcept readers of both documents. The header is included by the
 * public header and is not meant to be included on its own.
 */

#include "EnvironmentIsolation.hpp"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <utility>
#include <vector>

namespace appbox
{
namespace environment_isolation
{

/**
 * @brief Helpers of the JSON members of the two schemas.
 *
 * Every entry of both documents is read with the helpers below, so an entry of
 * the isolation file and an entry of the state file refuse a broken member the
 * same way. The helpers name the position of the entry in the error text, which
 * is the diagnostic of the schema and not a text an interface returns, and they
 * throw like the members of `from_json()` they serve.
 */
namespace detail
{

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
 * @tparam Mode Type of the mode to read.
 * @tparam ParseFn Type of the resolver of the token.
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

    if (const auto mode = parse(token))
    {
        return *mode;
    }

    isolation_document::Throw(prefix + "the member '" + key + "' is not a known mode: '" + token + "'");
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
 * @brief Read the version member of a document and refuse an unknown schema.
 *
 * The version is read before the rest of the document, so a file of another
 * schema is answered with the code of the version and not with the code of a
 * member which this schema does not carry.
 *
 * @param[in] json Object of the document.
 * @param[in] key Name of the version member.
 * @param[in] expected Version this build reads.
 * @return The success value, or the code of the refusal.
 */
inline tl::expected<void, ErrorCode> RequireVersion(const nlohmann::json& json, const char* key, int expected) noexcept
{
    const auto* version = isolation_document::FindMember(json, key);
    if (version == nullptr || !version->is_number_integer())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }
    if (version->get<int>() != expected)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentUnsupportedVersion);
    }

    return {};
}

} // namespace detail

/* Definition of the contract on the declaration. */
inline bool IsPathVariableName(std::wstring_view name) noexcept
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

/* Definition of the contract on the declaration. */
inline const char* IsolationToken(EnvironmentIsolation isolation) noexcept
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

/* Definition of the contract on the declaration. */
inline tl::expected<EnvironmentIsolation, ErrorCode> ParseIsolationToken(std::string_view token) noexcept
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
        return EnvironmentIsolation::Full;
    }
    if (normalized == "write_copy" || normalized == "writecopy")
    {
        return EnvironmentIsolation::WriteCopy;
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kIsolationTokenUnknown);
}

/* Definition of the contract on the declaration. */
inline const char* MergeModeToken(EnvironmentMergeMode merge) noexcept
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

/* Definition of the contract on the declaration. */
inline tl::expected<EnvironmentMergeMode, ErrorCode> ParseMergeModeToken(std::string_view token) noexcept
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
        return EnvironmentMergeMode::Replace;
    }
    if (normalized == "host")
    {
        return EnvironmentMergeMode::Host;
    }
    if (normalized == "prepend")
    {
        return EnvironmentMergeMode::Prepend;
    }
    if (normalized == "append")
    {
        return EnvironmentMergeMode::Append;
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kIsolationTokenUnknown);
}

/* Definition of the contract on the declaration. */
inline bool HostValueIsVisible(EnvironmentIsolation isolation) noexcept
{
    return isolation == EnvironmentIsolation::WriteCopy;
}

/* Definition of the contract on the declaration. */
inline ComposedValue ComposeEnvironmentValue(bool host_present, std::wstring_view host_value, std::wstring_view value,
                                             EnvironmentIsolation isolation, EnvironmentMergeMode merge,
                                             std::wstring_view merge_string) noexcept
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

/* Definition of the contract on the declaration. */
inline void to_json(nlohmann::json& json, const Entry& entry)
{
    json = nlohmann::json::object();
    json[kNameKey] = entry.name;
    json[kValueKey] = entry.value;
    json[kIsolationKey] = IsolationToken(entry.isolation);
    json[kMergeKey] = MergeModeToken(entry.merge);
    json[kMergeStringKey] = entry.merge_string;
}

/* Definition of the contract on the declaration. */
inline void ReadEntry(const nlohmann::json& json, std::size_t index, Entry& entry)
{
    const std::string prefix = detail::EntryPrefix(index);
    detail::RequireEntryObject(json, prefix);

    entry = Entry{};
    entry.name = detail::EntryText(json, kNameKey, prefix);
    entry.value = detail::EntryText(json, kValueKey, prefix);
    entry.merge_string = detail::EntryText(json, kMergeStringKey, prefix);
    entry.isolation = detail::EntryMode<EnvironmentIsolation>(json, kIsolationKey, prefix, ParseIsolationToken);
    entry.merge = detail::EntryMode<EnvironmentMergeMode>(json, kMergeKey, prefix, ParseMergeModeToken);

    if (entry.name.empty())
    {
        isolation_document::Throw(prefix + "the variable carries no name");
    }
    if (entry.name.find('=') != std::string::npos)
    {
        isolation_document::Throw(prefix + "the name of the variable carries an equals sign");
    }
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
    const std::string description = "the environment isolation file";
    isolation_document::RequireObject(json, description);

    document = Document{};
    document.version = isolation_document::RequiredInt(json, kVersionKey, description);

    const nlohmann::json& entries = detail::EntryList(json, kEntriesKey, description);
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        Entry entry;
        ReadEntry(entries[index], index, entry);
        document.entries.push_back(std::move(entry));
    }
}

/* Definition of the contract on the declaration. */
inline void to_json(nlohmann::json& json, const StateEntry& entry)
{
    json = nlohmann::json::object();
    json[kStateNameKey] = entry.name;
    json[kStateValueKey] = entry.value;
    json[kStateDeletedKey] = entry.deleted;
}

/* Definition of the contract on the declaration. */
inline void ReadEntry(const nlohmann::json& json, std::size_t index, StateEntry& entry)
{
    const std::string prefix = detail::EntryPrefix(index);
    detail::RequireEntryObject(json, prefix);

    entry = StateEntry{};
    entry.name = detail::EntryText(json, kStateNameKey, prefix);
    entry.value = detail::EntryText(json, kStateValueKey, prefix);
    entry.deleted = detail::EntryFlag(json, kStateDeletedKey, prefix);

    if (entry.name.empty())
    {
        isolation_document::Throw(prefix + "the variable carries no name");
    }
}

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
inline void from_json(const nlohmann::json& json, StateDocument& document)
{
    const std::string description = "the environment state file";
    isolation_document::RequireObject(json, description);

    document = StateDocument{};
    document.version = isolation_document::RequiredInt(json, kStateVersionKey, description);

    const nlohmann::json& entries = detail::EntryList(json, kStateEntriesKey, description);
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        StateEntry entry;
        ReadEntry(entries[index], index, entry);
        document.entries.push_back(std::move(entry));
    }
}

/* Definition of the contract on the declaration. */
inline tl::expected<Document, ErrorCode> ParseDocument(std::string_view text) noexcept
{
    /*
     * The JSON layer runs with the exception reporting switched off, so a text
     * which is no JSON document is answered by a discarded value, and the member
     * layer is the `from_json()` of the schema, which reports a refused member
     * through `IsolationDocumentError`. Both are turned into the code of the
     * table here, so the reader of a file never sees an exception and never
     * receives a text.
     */
    const auto parsed = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }

    if (const auto version = detail::RequireVersion(parsed, kVersionKey, kVersion); !version)
    {
        return tl::unexpected<ErrorCode>(version.error());
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

/* Definition of the contract on the declaration. */
inline tl::expected<StateDocument, ErrorCode> ParseStateDocument(std::string_view text) noexcept
{
    const auto parsed = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }

    if (const auto version = detail::RequireVersion(parsed, kStateVersionKey, kStateVersion); !version)
    {
        return tl::unexpected<ErrorCode>(version.error());
    }

    try
    {
        return parsed.get<StateDocument>();
    }
    catch (const std::exception&)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kIsolationDocumentInvalid);
    }
}

} // namespace environment_isolation
} // namespace appbox

#endif
