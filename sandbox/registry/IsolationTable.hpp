#ifndef APPBOX_SANDBOX_REGISTRY_ISOLATIONTABLE_HPP
#define APPBOX_SANDBOX_REGISTRY_ISOLATIONTABLE_HPP

#include "RegistryIsolation.hpp"
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace appbox
{
namespace registry
{

/**
 * @brief Case insensitive order of the key paths of the isolation table.
 *
 * Registered key names are case insensitive by definition, so the table treats
 * `HKEY_CURRENT_USER\Software` and `hkcu\software` as the same key.
 */
struct IsolationKeyLess
{
    /**
     * @brief Compare two key paths.
     * @param[in] left Left path.
     * @param[in] right Right path.
     * @return true when left has to be placed before right.
     */
    bool operator()(const std::wstring& left, const std::wstring& right) const;
};

/**
 * @brief One value of the isolation table, addressed by key and name.
 */
struct IsolationValueKey
{
    /**
     * @brief Path of the key which holds the value, relative to the hive root.
     */
    std::wstring key_path;

    /**
     * @brief Name of the value, empty for the default value of the key.
     */
    std::wstring value_name;
};

/**
 * @brief Case insensitive order of the value keys of the isolation table.
 */
struct IsolationValueKeyLess
{
    /**
     * @brief Compare two value keys.
     * @param[in] left Left value key.
     * @param[in] right Right value key.
     * @return true when left has to be placed before right.
     */
    bool operator()(const IsolationValueKey& left, const IsolationValueKey& right) const;
};

/**
 * @brief The isolation modes of the virtual registry inside the sandbox.
 *
 * The table is the sandbox side of the isolation file the packer writes next
 * to the hive. It holds the entries the file lists, which is every key and
 * every value of the packer workspace; the mode of every other entry — a key
 * the sandboxed process creates while it runs, or an entry of a hand written
 * file — is derived by walking the path upwards, so a key which was set to
 * `Full` also covers the keys and values below it which the file does not
 * list. An entry without any listed ancestor follows the default mode
 * `WriteCopy`, which is the behaviour of a sandbox without an isolation
 * file.
 *
 * The class holds no dependency on the Windows registry API, so the lookup
 * rules are unit testable.
 */
class IsolationTable
{
public:
    /**
     * @brief Load the table from the text of an isolation file.
     *
     * The call is atomic: the parsed modes are collected into a local table
     * first and replace the current content only when the whole document was
     * accepted. A failure therefore leaves the table unchanged.
     *
     * @param[in] text The UTF-8 text of the isolation file.
     * @param[out] error Error description on failure.
     * @return true when the file was parsed.
     */
    bool Parse(const std::string& text, std::string& error);

    /**
     * @brief Whether the table holds no entry.
     * @return true when neither a key nor a value mode is listed.
     */
    bool Empty() const;

    /**
     * @brief Number of listed key modes.
     * @return The number of key entries.
     */
    std::size_t KeyCount() const;

    /**
     * @brief Number of listed value modes.
     * @return The number of value entries.
     */
    std::size_t ValueCount() const;

    /**
     * @brief Get the effective isolation mode of a key.
     *
     * The lookup starts at the key itself and walks the path upwards until a
     * listed entry is found, so a listed ancestor covers its whole subtree.
     *
     * @param[in] key_path Path of the key relative to the hive root, for
     *                     example `HKEY_CURRENT_USER\Software`.
     * @return The mode of the key, `WriteCopy` when neither the key nor one of
     *         its ancestors is listed.
     */
    RegistryIsolation KeyMode(const std::wstring& key_path) const;

    /**
     * @brief Get the effective isolation mode of a value.
     *
     * A value which is not listed follows the effective mode of the key which
     * holds it.
     *
     * @param[in] key_path Path of the key which holds the value.
     * @param[in] value_name Name of the value, empty for the default value.
     * @return The mode of the value.
     */
    RegistryIsolation ValueMode(const std::wstring& key_path, const std::wstring& value_name) const;

    /**
     * @brief Whether a mode keeps the entry of the host registry invisible.
     *
     * `Full` and `Hide` both describe an entry the sandboxed process may only
     * see through the virtual registry: a key or value which the hive does not
     * hold is reported as not found instead of falling back to the host.
     * `WriteCopy` keeps the host entry visible.
     *
     * @param[in] mode The isolation mode to classify.
     * @return true when the host entry must stay invisible.
     */
    static bool HidesHost(RegistryIsolation mode);

private:
    /**
     * @brief The listed key modes, ordered by path.
     */
    std::map<std::wstring, RegistryIsolation, IsolationKeyLess> keys_;

    /**
     * @brief The listed value modes, ordered by key path and value name.
     */
    std::map<IsolationValueKey, RegistryIsolation, IsolationValueKeyLess> values_;
};

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_ISOLATIONTABLE_HPP
