#ifndef APPBOX_COMMON_DETAILS_REGISTRY_ISOLATION_DETAIL_HPP
#define APPBOX_COMMON_DETAILS_REGISTRY_ISOLATION_DETAIL_HPP

/*
 * Implementation of `common/RegistryIsolation.hpp`: the comparison of the root
 * key names, the token parsing of the modes, the JSON members of the schema and
 * the noexcept reader of a document. The header is included by the public header
 * and is not meant to be included on its own.
 */

#include "RegistryIsolation.hpp"
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
namespace registry_isolation
{

/* Definition of the contract on the declaration. */
inline bool EqualsRootKeyName(const std::wstring& name, const wchar_t* root) noexcept
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
inline bool IsRootKeyName(const std::wstring& name) noexcept
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
inline const char* KeyIsolationToken(RegistryKeyIsolation isolation) noexcept
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
inline const char* ValueIsolationToken(RegistryValueIsolation isolation) noexcept
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
inline std::string NormalizeIsolationToken(std::string_view token) noexcept
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

    return normalized;
}

/* Definition of the contract on the declaration. */
inline tl::expected<RegistryKeyIsolation, ErrorCode> ParseKeyIsolationToken(std::string_view token) noexcept
{
    const std::string normalized = NormalizeIsolationToken(token);

    if (normalized == "full")
    {
        return RegistryKeyIsolation::Full;
    }
    if (normalized == "merge")
    {
        return RegistryKeyIsolation::Merge;
    }
    if (normalized == "write_copy" || normalized == "writecopy")
    {
        return RegistryKeyIsolation::WriteCopy;
    }
    if (normalized == "hide")
    {
        return RegistryKeyIsolation::Hide;
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kIsolationTokenUnknown);
}

/* Definition of the contract on the declaration. */
inline tl::expected<RegistryValueIsolation, ErrorCode> ParseValueIsolationToken(std::string_view token) noexcept
{
    const std::string normalized = NormalizeIsolationToken(token);

    if (normalized == "full")
    {
        return RegistryValueIsolation::Full;
    }
    if (normalized == "hide")
    {
        return RegistryValueIsolation::Hide;
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kIsolationTokenUnknown);
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
     * A key entry names no value, but a document which carries a name of another
     * type is refused like a value entry, so both lists of the file follow the
     * same rules.
     */
    const auto* name = isolation_document::FindMember(json, kNameKey);
    if (name != nullptr && !name->is_string())
    {
        isolation_document::Throw("the '" + std::string(kNameKey) + "' member of " + holder + " is not a string");
    }

    const std::string isolation_token = isolation_document::RequiredText(json, kIsolationKey, holder);
    if (const auto parsed = ParseKeyIsolationToken(isolation_token))
    {
        entry.isolation = *parsed;
        return;
    }

    isolation_document::Throw("unknown key isolation mode '" + isolation_token + "' in the isolation file");
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

    const std::string isolation_token = isolation_document::RequiredText(json, kIsolationKey, holder);
    if (const auto parsed = ParseValueIsolationToken(isolation_token))
    {
        entry.isolation = *parsed;
        return;
    }

    isolation_document::Throw("unknown value isolation mode '" + isolation_token + "' in the isolation file");
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

} // namespace registry_isolation
} // namespace appbox

#endif
