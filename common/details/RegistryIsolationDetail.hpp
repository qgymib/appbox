#ifndef APPBOX_COMMON_DETAILS_REGISTRY_ISOLATION_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_REGISTRY_ISOLATION_DETAIL_HPP

/*
 * Implementation of `common/RegistryIsolation.hpp`: the comparison of the root
 * key names, the token parsing of the modes and the JSON members of the schema.
 * The header is included by the public header and is not meant to be included on
 * its own.
 */

#include "RegistryIsolation.hpp"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace appbox
{
namespace registry_isolation
{

/* Definition of the contract on the declaration. */
inline bool EqualsRootKeyName(const std::wstring& name, const wchar_t* root)
{
    if (name.empty() || root == nullptr)
    {
        return false;
    }

    for (std::size_t index = 0;; ++index)
    {
        wchar_t left = name[index];
        wchar_t right = root[index];

        if (left >= L'A' && left <= L'Z')
        {
            left = static_cast<wchar_t>(left - L'A' + L'a');
        }
        if (right >= L'A' && right <= L'Z')
        {
            right = static_cast<wchar_t>(right - L'A' + L'a');
        }

        if (left != right)
        {
            return false;
        }
        if (left == L'\0')
        {
            return true;
        }
    }
}

/* Definition of the contract on the declaration. */
inline bool IsRootKeyName(const std::wstring& name)
{
    for (const wchar_t* root : kRootKeyNames)
    {
        if (EqualsRootKeyName(name, root))
        {
            return true;
        }
    }
    return false;
}

/* Definition of the contract on the declaration. */
inline const char* KeyIsolationToken(RegistryKeyIsolation isolation)
{
    switch (isolation)
    {
    case RegistryKeyIsolation::Full:
        return "full";
    case RegistryKeyIsolation::Merge:
        return "merge";
    case RegistryKeyIsolation::WriteCopy:
        return "write_copy";
    case RegistryKeyIsolation::Hide:
        return "hide";
    }
    return "merge";
}

/* Definition of the contract on the declaration. */
inline const char* ValueIsolationToken(RegistryValueIsolation isolation)
{
    switch (isolation)
    {
    case RegistryValueIsolation::Full:
        return "full";
    case RegistryValueIsolation::Hide:
        return "hide";
    }
    return "full";
}

/* Definition of the contract on the declaration. */
inline void NormalizeIsolationToken(std::string_view token, std::string& normalized)
{
    normalized.clear();
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
}

/* Definition of the contract on the declaration. */
inline bool ParseKeyIsolationToken(std::string_view token, RegistryKeyIsolation& out)
{
    std::string normalized;
    NormalizeIsolationToken(token, normalized);

    if (normalized == "full")
    {
        out = RegistryKeyIsolation::Full;
        return true;
    }
    if (normalized == "merge")
    {
        out = RegistryKeyIsolation::Merge;
        return true;
    }
    if (normalized == "write_copy" || normalized == "writecopy")
    {
        out = RegistryKeyIsolation::WriteCopy;
        return true;
    }
    if (normalized == "hide")
    {
        out = RegistryKeyIsolation::Hide;
        return true;
    }
    return false;
}

/* Definition of the contract on the declaration. */
inline bool ParseValueIsolationToken(std::string_view token, RegistryValueIsolation& out)
{
    std::string normalized;
    NormalizeIsolationToken(token, normalized);

    if (normalized == "full")
    {
        out = RegistryValueIsolation::Full;
        return true;
    }
    if (normalized == "hide")
    {
        out = RegistryValueIsolation::Hide;
        return true;
    }
    return false;
}

/* Definition of the contract on the declaration. */
inline void to_json(nlohmann::json& json, const KeyEntry& entry)
{
    json = nlohmann::json::object();
    json[kPathKey] = entry.path;
    json[kIsolationKey] = KeyIsolationToken(entry.isolation);
}

/* Definition of the contract on the declaration. */
inline void to_json(nlohmann::json& json, const ValueEntry& entry)
{
    json = nlohmann::json::object();
    json[kPathKey] = entry.path;
    json[kNameKey] = entry.name;
    json[kIsolationKey] = ValueIsolationToken(entry.isolation);
}

/* Definition of the contract on the declaration. */
inline void from_json(const nlohmann::json& json, KeyEntry& entry)
{
    const std::string holder = "an isolation file entry";
    isolation_document::RequireObject(json, holder);

    entry.path = isolation_document::RequiredText(json, kPathKey, holder);
    if (entry.path.empty())
    {
        isolation_document::Throw("an isolation file entry has an empty key path");
    }

    /*
     * A key entry names no value, but a document which carries a name of
     * another type is refused like a value entry, so both lists of the file
     * follow the same rules.
     */
    const auto* name = isolation_document::FindMember(json, kNameKey);
    if (name != nullptr && !name->is_string())
    {
        isolation_document::Throw("the '" + std::string(kNameKey) + "' member of " + holder + " is not a string");
    }

    const std::string    isolation_token = isolation_document::RequiredText(json, kIsolationKey, holder);
    RegistryKeyIsolation isolation = RegistryKeyIsolation::Merge;
    if (!ParseKeyIsolationToken(isolation_token, isolation))
    {
        isolation_document::Throw("unknown key isolation mode '" + isolation_token + "' in the isolation file");
    }

    entry.isolation = isolation;
}

/* Definition of the contract on the declaration. */
inline void from_json(const nlohmann::json& json, ValueEntry& entry)
{
    const std::string holder = "an isolation file entry";
    isolation_document::RequireObject(json, holder);

    entry.path = isolation_document::RequiredText(json, kPathKey, holder);
    if (entry.path.empty())
    {
        isolation_document::Throw("an isolation file entry has an empty key path");
    }

    entry.name = isolation_document::RequiredText(json, kNameKey, holder);

    const std::string      isolation_token = isolation_document::RequiredText(json, kIsolationKey, holder);
    RegistryValueIsolation isolation = RegistryValueIsolation::Full;
    if (!ParseValueIsolationToken(isolation_token, isolation))
    {
        isolation_document::Throw("unknown value isolation mode '" + isolation_token + "' in the isolation file");
    }

    entry.isolation = isolation;
}

/* Definition of the contract on the declaration. */
inline void to_json(nlohmann::json& json, const Document& document)
{
    json = nlohmann::json::object();
    json[kVersionKey] = document.version;

    nlohmann::json keys = nlohmann::json::array();
    for (const auto& key : document.keys)
    {
        keys.push_back(nlohmann::json(key));
    }
    json[kKeysKey] = std::move(keys);

    nlohmann::json values = nlohmann::json::array();
    for (const auto& value : document.values)
    {
        values.push_back(nlohmann::json(value));
    }
    json[kValuesKey] = std::move(values);
}

/* Definition of the contract on the declaration. */
inline void from_json(const nlohmann::json& json, Document& document)
{
    const std::string holder = "the isolation file";
    isolation_document::RequireObject(json, holder);

    document = Document{};
    document.version = isolation_document::RequiredInt(json, kVersionKey, holder);

    if (const auto* keys = isolation_document::OptionalArray(json, kKeysKey, holder))
    {
        for (const auto& item : *keys)
        {
            document.keys.push_back(item.get<KeyEntry>());
        }
    }

    if (const auto* values = isolation_document::OptionalArray(json, kValuesKey, holder))
    {
        for (const auto& item : *values)
        {
            document.values.push_back(item.get<ValueEntry>());
        }
    }
}

} // namespace registry_isolation
} // namespace appbox

#endif
