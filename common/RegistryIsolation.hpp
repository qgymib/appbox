#ifndef APPBOX_COMMON_REGISTRY_ISOLATION_HPP
#define APPBOX_COMMON_REGISTRY_ISOLATION_HPP

#include "IsolationDocument.hpp"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace appbox
{

/**
 * @brief Isolation mode of a registry key.
 *
 * The mode describes how a sandboxed process sees the key, the sub keys and
 * the values below it, and where a modification of the key lands:
 *
 * - `Full` - the host entry is invisible: the sandboxed process sees the sub
 *   keys and the values of the virtual registry alone, and every modification
 *   of the key lands in the sandbox.
 * - `Merge` - the host entry and the virtual registry are both visible, with
 *   the virtual registry taking precedence. A modification lands in the layer
 *   which holds the entry: a key or a value the sandbox does not hold is
 *   modified in the **real registry**, while an entry the sandbox holds is
 *   modified in the sandbox. A value which the sandbox deleted stays in the
 *   sandbox for that rule, because the deletion is recorded there. `Merge` is
 *   the only mode which modifies the real registry.
 * - `WriteCopy` - the host entry and the virtual registry are both visible,
 *   with the virtual registry taking precedence, and every modification lands
 *   in the sandbox: a key which the host holds but the sandbox does not is
 *   copied up into the sandbox first.
 * - `Hide` - the key, its sub keys and its values are invisible, even while
 *   the host holds them: a read reports `Key Not Found`, a write lands in the
 *   sandbox, and the entry is visible from then on. The content a layer packed
 *   for a hidden entry stays hidden as well, see the hidden store of the hive.
 *
 * A key never has a default mode: an entry which the isolation file does not
 * list follows the closest listed key above it, and the file has to list the
 * root keys of the view, so the mode of every path resolves. `Hide` covers the
 * whole subtree of the key: an entry below a hidden key is hidden as well,
 * whatever mode it names itself.
 */
enum class RegistryKeyIsolation
{
    Full,  ///< Host invisible, every modification lands in the sandbox.
    Merge, ///< Host visible with the sandbox taking precedence, a modification of an entry the sandbox does not hold
           ///< lands in the host.
    WriteCopy, ///< Host visible with the sandbox taking precedence, every modification lands in the sandbox.
    Hide       ///< The key and its whole subtree are invisible until the sandbox writes them.
};

/**
 * @brief Isolation mode of a registry value.
 *
 * The mode of a value decides whether the value of the host registry stays
 * visible and whether the packed value of the virtual registry is part of the
 * view. A write of the value always lands in the sandbox, so a value mode
 * never modifies the real registry:
 *
 * - `Full` - the host value is invisible; the value of the virtual registry is
 *   what the sandboxed process sees, and a value which the virtual registry
 *   does not hold is reported as missing.
 * - `Hide` - the value is invisible, even while the virtual registry holds it:
 *   a read reports `Key Not Found`, a write lands in the sandbox, and the
 *   written value is visible from then on.
 *
 * A value which the isolation file does not list follows the mode of the key
 * which holds it, so a value below a `Merge` key is merged with the host like
 * the key itself.
 */
enum class RegistryValueIsolation
{
    Full, ///< The host value is invisible, the value of the virtual registry is visible.
    Hide  ///< The value is invisible until the sandbox writes it.
};

/**
 * @brief Schema of the registry isolation file.
 *
 * The file is the JSON document which carries the isolation modes of the
 * virtual registry from the packer to the sandbox: the packer writes it next
 * to the hive inside the overlay, the sandbox reads it when it mounts the
 * hive. The packer lists every key and every value of the workspace, the five
 * root keys of the view included, so the mode of every entry resolves without
 * a default.
 *
 * ```
 * {
 *   "version": 1,
 *   "keys":   [ { "path": "HKEY_CURRENT_USER", "isolation": "merge" },
 *               { "path": "HKEY_CURRENT_USER\\Software\\Vendor",
 *                 "isolation": "full" } ],
 *   "values": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor",
 *                 "name": "Server", "isolation": "hide" } ]
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
 * The version is the first member of the document and has to match exactly
 * while the file is read, so a file of another schema is refused instead of
 * being interpreted with the rules of this one.
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
 * @brief Number of root keys of the view.
 *
 * The five root keys are the preset nodes of the registry workspace and the
 * first component of every path of the view, so they are the entries a file
 * has to list for the mode of every path to resolve.
 */
inline constexpr std::size_t kRootKeyCount = 5;

/**
 * @brief The names of the five root keys of the view, in canonical order.
 *
 * The list is the single source of the root keys: the model of the packer
 * builds its fixed nodes from it and the sandbox checks that a loaded
 * isolation table names all of them.
 */
inline constexpr const wchar_t* kRootKeyNames[kRootKeyCount] = { L"HKEY_CLASSES_ROOT", L"HKEY_CURRENT_USER",
                                                                 L"HKEY_LOCAL_MACHINE", L"HKEY_USERS",
                                                                 L"HKEY_CURRENT_CONFIG" };

/**
 * @brief Compare a name with a root key name ignoring the case.
 * @param[in] name The name to test.
 * @param[in] root The root key name to compare with.
 * @return true when both names are equal ignoring the case.
 */
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

/**
 * @brief Whether a name is the name of one of the five root keys of the view.
 * @param[in] name The name to test.
 * @return true when the name names a root key.
 */
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

/**
 * @brief Get the token of a key isolation mode.
 * @param[in] isolation The isolation mode.
 * @return The canonical lower case token, for example `"write_copy"`.
 */
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

/**
 * @brief Get the token of a value isolation mode.
 * @param[in] isolation The isolation mode.
 * @return The canonical lower case token, for example `"full"`.
 */
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

/**
 * @brief Normalize a mode token for the comparison.
 *
 * The comparison of a token ignores the case and accepts spaces and dashes in
 * place of the underscore of the canonical token, so `L"Write Copy"` and
 * `L"write-copy"` resolve as well. The mode tokens are ASCII, so the tokens
 * are handled as narrow text like the JSON document which carries them.
 *
 * @param[in] token The token to normalize.
 * @param[out] normalized The lower case token with underscores.
 */
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

/**
 * @brief Resolve a key isolation mode from its token.
 * @param[in] token The token to resolve.
 * @param[out] out The resolved mode when the token is known.
 * @return true when the token names a key isolation mode.
 */
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

/**
 * @brief Resolve a value isolation mode from its token.
 *
 * The two value modes are `full` and `hide`; a token of a key only mode
 * (`merge` or `write_copy`) is refused, because a value write never reaches
 * the real registry and a value is never copied up.
 *
 * @param[in] token The token to resolve.
 * @param[out] out The resolved mode when the token is known.
 * @return true when the token names a value isolation mode.
 */
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
     *
     * The default is the mode of a preset node of the workspace, which is
     * `Merge`: a file which lists the root keys without naming a mode keeps
     * the host registry of the machine visible.
     */
    RegistryKeyIsolation isolation = RegistryKeyIsolation::Merge;
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
     *
     * The default is the mode the workspace gives a new value, which is
     * `Full`: the host value is invisible and the virtual registry defines it.
     */
    RegistryValueIsolation isolation = RegistryValueIsolation::Full;
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
    json[kIsolationKey] = KeyIsolationToken(entry.isolation);
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
    json[kIsolationKey] = ValueIsolationToken(entry.isolation);
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

    const std::string    isolation_token = isolation_document::RequiredText(json, kIsolationKey, holder);
    RegistryKeyIsolation isolation = RegistryKeyIsolation::Merge;
    if (!ParseKeyIsolationToken(isolation_token, isolation))
    {
        isolation_document::Throw("unknown key isolation mode '" + isolation_token + "' in the isolation file");
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

    const std::string      isolation_token = isolation_document::RequiredText(json, kIsolationKey, holder);
    RegistryValueIsolation isolation = RegistryValueIsolation::Full;
    if (!ParseValueIsolationToken(isolation_token, isolation))
    {
        isolation_document::Throw("unknown value isolation mode '" + isolation_token + "' in the isolation file");
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
 * @brief Layout of the marker store inside the sandbox hive.
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

/**
 * @brief Layout of the hidden store inside the sandbox hive.
 *
 * A key or a value whose mode is `Hide` is invisible for the sandboxed
 * process, and the content a layer packed for it must stay invisible as well:
 * a hive is one file which carries the packed content and the writes of the
 * run, so the packed content of a hidden entry cannot be told apart from a
 * write the sandbox made. The packer therefore writes the content of a hidden
 * entry into the **hidden store** instead of the path of the view, and the
 * sandbox never maps a path of the view onto it:
 *
 * ```
 * <root of the hive>
 * ├── HKEY_CURRENT_USER\Software\Vendor          the view: the writes of the run
 * └── APPBOX_HIDDEN                              the packed content of hidden entries
 *     └── HKEY_CURRENT_USER\Software\Vendor      the same shape as the view
 * ```
 *
 * The store mirrors the structure of the view, so the packer writes a hidden
 * entry at the same relative path below the store key. A write of the
 * sandboxed process lands in the view, which is what makes a hidden entry
 * visible once the sandbox writes it; the content of the store stays invisible
 * until a layer gives the entry a mode which is not `Hide`, which the launcher
 * applies while it merges the layers of the run.
 */
namespace registry_hidden
{

/**
 * @brief Name of the reserved top level key of the hive which holds the
 *        content of the hidden entries.
 *
 * The key is a sub key of the hive root and therefore a sibling of the five
 * root keys of the view and of the whiteout store. It is never part of the
 * view: no path of the view maps onto it and no handle of it is ever handed
 * out to the sandboxed process.
 */
inline constexpr const wchar_t* kStoreKey = L"APPBOX_HIDDEN";

} // namespace registry_hidden

} // namespace appbox

#endif // APPBOX_COMMON_REGISTRY_ISOLATION_HPP
