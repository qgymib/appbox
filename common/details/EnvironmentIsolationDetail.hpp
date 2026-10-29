#ifndef APPBOX_COMMON_DETAILS_ENVIRONMENT_ISOLATION_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_ENVIRONMENT_ISOLATION_DETAIL_HPP

/*
 * Implementation of `common/EnvironmentIsolation.hpp`: the token parsing, the
 * composition of a value and the JSON members of the isolation and the state
 * schema. The header is included by the public header and is not meant to be
 * included on its own.
 */

#include "EnvironmentIsolation.hpp"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace appbox
{
namespace environment_isolation
{

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
inline bool HostValueIsVisible(EnvironmentIsolation isolation)
{
    return isolation == EnvironmentIsolation::WriteCopy;
}

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
inline std::string EntryPrefix(std::size_t index)
{
    return "entries[" + std::to_string(index) + "]: ";
}

/* Definition of the contract on the declaration. */
inline std::string EntryText(const nlohmann::json& entry, const char* key, const std::string& prefix)
{
    const auto* member = isolation_document::FindMember(entry, key);
    if (member == nullptr || !member->is_string())
    {
        isolation_document::Throw(prefix + "the member '" + key + "' is missing or is not a string");
    }

    return member->get<std::string>();
}

/* Definition of the contract on the declaration. */
inline bool EntryFlag(const nlohmann::json& entry, const char* key, const std::string& prefix)
{
    const auto* member = isolation_document::FindMember(entry, key);
    if (member == nullptr || !member->is_boolean())
    {
        isolation_document::Throw(prefix + "the member '" + key + "' is missing or is not a boolean");
    }

    return member->get<bool>();
}

/* Definition of the contract on the declaration. */
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

/* Definition of the contract on the declaration. */
inline const nlohmann::json& EntryList(const nlohmann::json& json, const char* key, const std::string& description)
{
    const auto* entries = isolation_document::FindMember(json, key);
    if (entries == nullptr || !entries->is_array())
    {
        isolation_document::Throw(description + " carries no entry list");
    }

    return *entries;
}

/* Definition of the contract on the declaration. */
inline void RequireEntryObject(const nlohmann::json& entry, const std::string& prefix)
{
    if (!entry.is_object())
    {
        isolation_document::Throw(prefix + "the entry is not an object");
    }
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

    const nlohmann::json& entries = EntryList(json, kEntriesKey, description);
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

#endif
