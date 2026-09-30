#ifndef APPBOX_COMMON_REGISTRY_ISOLATION_HPP
#define APPBOX_COMMON_REGISTRY_ISOLATION_HPP

#include "IsolationDocument.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace appbox
{

/**
 * @brief Isolation mode of a registry key or value.
 *
 * The modes describe how a sandboxed process sees the entry. No mode ever
 * modifies the real registry: every write of the sandboxed process lands in
 * the overlay hive.
 *
 * - `Full` - only the sandbox registry is visible, every access is redirected
 *   into the overlay hive even when the host holds the entry.
 * - `WriteCopy` - both registries are visible with the sandbox taking
 *   precedence, and every write is redirected into the sandbox: a key which
 *   the host holds but the hive does not is copied up into the hive as soon as
 *   the caller asks for write access. This is the default mode of the
 *   workspace.
 * - `Hide` - the entry is invisible for the sandboxed process: opening,
 *   reading and writing report `Key Not Found`, while creating the entry
 *   succeeds inside the sandbox.
 *
 * The enumeration lives in `common/` because the packer and the sandbox share
 * it: the packer writes the modes into the isolation file, the sandbox reads
 * them back and enforces them.
 */
enum class RegistryIsolation
{
    Full,      ///< Sandbox only.
    WriteCopy, ///< Host and sandbox with sandbox precedence, writes copied up.
    Hide       ///< Invisible for the sandbox, creation lands in the sandbox.
};

/**
 * @brief Schema of the registry isolation file.
 *
 * The file is the JSON document which carries the isolation modes of the
 * virtual registry from the packer to the sandbox: the packer writes it next
 * to the hive inside the overlay, the sandbox reads it when it mounts the
 * hive. The packer lists every key and every value of the workspace; an entry
 * which is not listed follows the closest listed key above it and falls back
 * to `WriteCopy` when no ancestor is listed at all.
 *
 * ```
 * {
 *   "version": 1,
 *   "keys":   [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor",
 *                 "isolation": "full" } ],
 *   "values": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor",
 *                 "name": "Server", "isolation": "full" } ]
 * }
 * ```
 *
 * The path of an entry is the key path of the virtual registry, which is the
 * path relative to the root of the hive (the hive holds one sub key per root
 * key), so the paths of the model can be used unchanged.
 */
namespace registry_isolation
{

/**
 * @brief Version of the isolation file written by the packer.
 *
 * A file of a different version is rejected instead of being interpreted with
 * the rules of another schema.
 */
inline constexpr int kVersion = 1;

/** Member name of the schema version. */
inline constexpr const char* kVersionKey = "version";

/** Member name of the key mode list. */
inline constexpr const char* kKeysKey = "keys";

/** Member name of the value mode list. */
inline constexpr const char* kValuesKey = "values";

/** Member name of the key path of an entry. */
inline constexpr const char* kPathKey = "path";

/** Member name of the value name of a value entry. */
inline constexpr const char* kNameKey = "name";

/** Member name of the isolation mode of an entry. */
inline constexpr const char* kIsolationKey = "isolation";

/**
 * @brief Get the token of an isolation mode.
 * @param[in] isolation The isolation mode.
 * @return The canonical lower case token, for example `"write_copy"`.
 */
inline const char* IsolationToken(RegistryIsolation isolation)
{
    switch (isolation)
    {
    case RegistryIsolation::Full:
        return "full";
    case RegistryIsolation::WriteCopy:
        return "write_copy";
    case RegistryIsolation::Hide:
        return "hide";
    }
    return "write_copy";
}

/**
 * @brief Resolve an isolation mode from its token.
 *
 * The comparison ignores the case and accepts spaces and dashes in place of
 * the underscore of the canonical token, so `L"Write Copy"` and
 * `L"write-copy"` resolve as well. The mode tokens are ASCII, so the tokens
 * are handled as narrow text like the JSON document which carries them.
 *
 * @param[in] token The token to resolve.
 * @param[out] out The resolved mode when the token is known.
 * @return true when the token names an isolation mode.
 */
inline bool ParseIsolationToken(std::string_view token, RegistryIsolation& out)
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
        out = RegistryIsolation::Full;
        return true;
    }
    if (normalized == "write_copy" || normalized == "writecopy")
    {
        out = RegistryIsolation::WriteCopy;
        return true;
    }
    if (normalized == "hide")
    {
        out = RegistryIsolation::Hide;
        return true;
    }
    return false;
}

/**
 * @brief One key entry of the registry isolation file.
 *
 * The structure is the schema of one listed key: the packer fills it while it
 * writes the file of the workspace, and the sandbox reads the file back into
 * the same structure, so neither side parses the JSON object of an entry
 * member by member.
 */
struct KeyEntry
{
    /**
     * @brief Path of the key inside the virtual registry, in UTF-8.
     *
     * The path is relative to the root of the hive: its first component is the
     * name of one of the five root keys of the view.
     */
    std::string path;

    /**
     * @brief Isolation mode of the key and of everything below it.
     */
    RegistryIsolation isolation = RegistryIsolation::WriteCopy;
};

/**
 * @brief One value entry of the registry isolation file.
 */
struct ValueEntry
{
    /**
     * @brief Path of the key which holds the value, in UTF-8.
     */
    std::string path;

    /**
     * @brief Name of the value, empty for the default value of the key.
     */
    std::string name;

    /**
     * @brief Isolation mode of the value.
     */
    RegistryIsolation isolation = RegistryIsolation::WriteCopy;
};

/**
 * @brief The content of the registry isolation file.
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
     * @brief The listed keys, in the order of the file.
     */
    std::vector<KeyEntry> keys;

    /**
     * @brief The listed values, in the order of the file.
     */
    std::vector<ValueEntry> values;
};

/**
 * @brief Store one key entry of the registry isolation file.
 * @param[out] json Object which receives the entry.
 * @param[in] entry The entry to store.
 */
inline void to_json(nlohmann::json& json, const KeyEntry& entry)
{
    json = nlohmann::json::object();
    json[kPathKey] = entry.path;
    json[kIsolationKey] = IsolationToken(entry.isolation);
}

/**
 * @brief Store one value entry of the registry isolation file.
 * @param[out] json Object which receives the entry.
 * @param[in] entry The entry to store.
 */
inline void to_json(nlohmann::json& json, const ValueEntry& entry)
{
    json = nlohmann::json::object();
    json[kPathKey] = entry.path;
    json[kNameKey] = entry.name;
    json[kIsolationKey] = IsolationToken(entry.isolation);
}

/**
 * @brief Read one key entry of the registry isolation file.
 * @param[in] json Object holding the entry.
 * @param[out] entry The entry to fill.
 * @throw appbox::IsolationDocumentError The entry does not fit the schema.
 */
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

    const std::string isolation_token = isolation_document::RequiredText(json, kIsolationKey, holder);
    RegistryIsolation isolation = RegistryIsolation::WriteCopy;
    if (!ParseIsolationToken(isolation_token, isolation))
    {
        isolation_document::Throw("unknown isolation mode '" + isolation_token + "' in the isolation file");
    }

    entry.isolation = isolation;
}

/**
 * @brief Read one value entry of the registry isolation file.
 * @param[in] json Object holding the entry.
 * @param[out] entry The entry to fill.
 * @throw appbox::IsolationDocumentError The entry does not fit the schema.
 */
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
    RegistryIsolation isolation = RegistryIsolation::WriteCopy;
    if (!ParseIsolationToken(isolation_token, isolation))
    {
        isolation_document::Throw("unknown isolation mode '" + isolation_token + "' in the isolation file");
    }

    entry.isolation = isolation;
}

/**
 * @brief Store the content of the registry isolation file.
 * @param[out] json Object which receives the document.
 * @param[in] document The document to store.
 */
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

/**
 * @brief Read the content of the registry isolation file.
 *
 * A document which does not list a key or a value describes a file which sets
 * no mode at all, which is what a session without a mode writes.
 *
 * @param[in] json Object holding the document.
 * @param[out] document The document to fill.
 * @throw appbox::IsolationDocumentError The document does not fit the schema.
 */
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

/**
 * @brief Layout of the whiteout store inside the sandbox hive.
 *
 * The registry isolation has no way to remove an entry of the host registry,
 * so a delete of a sandboxed process is recorded as a **whiteout**: the entry
 * disappears from the merged view of the sandbox without touching the host.
 * The markers live in a reserved key at the root of the hive, next to the five
 * root keys of the view, and the key is not part of the view at all:
 *
 * ```
 * <root of the hive>
 * ├── HKEY_CLASSES_ROOT          the five root keys of the view
 * ├── ...
 * └── APPBOX_WHITEOUT            the whiteout store, invisible for the view
 *     ├── K\HKEY_CURRENT_USER\Software\Vendor    the key was deleted
 *     └── V\HKEY_CURRENT_USER\Software\Vendor    holds one value per deleted
 *                                                value name (an empty name is
 *                                                the default value)
 * ```
 *
 * The store is shared knowledge: the sandbox writes and reads it, and the
 * launcher has to skip it while it enumerates the hive, so both sides use the
 * names below instead of a literal.
 */
namespace registry_whiteout
{

/**
 * @brief Name of the reserved top level key of the hive which holds the
 *        whiteout markers.
 *
 * The key is a sub key of the hive root and therefore a sibling of the five
 * root keys of the view. It is never part of the view: no path of the view
 * maps onto it and no handle of it is ever handed out to the sandboxed
 * process.
 */
inline constexpr const wchar_t* kStoreKey = L"APPBOX_WHITEOUT";

/**
 * @brief Name of the sub key of the store which holds the deleted keys.
 *
 * A key below it names the hive relative path of a deleted key.
 */
inline constexpr const wchar_t* kKeysKey = L"K";

/**
 * @brief Name of the sub key of the store which holds the deleted values.
 *
 * A key below it names the hive relative path of the key which held the
 * deleted values; the deleted value names are the value names of that key,
 * which makes the empty name of the default value work like every other name.
 */
inline constexpr const wchar_t* kValuesKey = L"V";

} // namespace registry_whiteout

} // namespace appbox

#endif // APPBOX_COMMON_REGISTRY_ISOLATION_HPP
