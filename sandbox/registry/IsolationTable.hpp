#ifndef APPBOX_SANDBOX_REGISTRY_ISOLATIONTABLE_HPP
#define APPBOX_SANDBOX_REGISTRY_ISOLATIONTABLE_HPP

#include "RegistryIsolation.hpp"
#include <cstddef>
#include <map>
#include <optional>
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
 * The table is the sandbox side of the isolation files of the layers of a run.
 * It holds the entries those files list, which is every key and every value of
 * the packer workspace; the mode of every other entry — a key the sandboxed
 * process creates while it runs, or an entry of a hand written file — is
 * derived by walking the path upwards. There is no default mode: the file of
 * the archive has to list the five root keys of the view (see
 * `NamesRootKeys()`), so the mode of every path of the view resolves. A table
 * which misses a root key was never validated and reports `Hide`, which keeps
 * the host registry out of the view instead of showing it.
 *
 * The files are applied one after the other, the file of the resources of the
 * archive first and the file of every patch package after it, so a mode a
 * later file sets for a key or a value overrides the mode the files below it
 * set for the same entry while an entry no later file lists keeps the mode of
 * the layers below it.
 *
 * The class holds no dependency on the Windows registry API, so the lookup
 * rules are unit testable.
 */
class IsolationTable
{
public:
    /**
     * @brief Apply the text of an isolation file as the layer it describes.
     *
     * The modes the document lists replace the modes of the same entry of the
     * layers below it, while the entries the document does not list stay in
     * place. The call is atomic per document: the parsed modes are collected
     * into local tables first and are written into the table only when the
     * whole document was accepted, so a document which cannot be used leaves
     * the table as it was.
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
     * @brief Whether the table names every root key of the view.
     *
     * The five root keys are the first component of every path of the view, so
     * a table which names all of them resolves the mode of every entry. A
     * loaded table which misses one of them cannot describe the run and fails
     * the start of the sandbox.
     *
     * @return true when all five root keys are listed.
     */
    bool NamesRootKeys() const;

    /**
     * @brief Get the effective isolation mode of a key.
     *
     * The lookup starts at the key itself and walks the path upwards until a
     * listed entry is found, so a listed ancestor covers its whole subtree.
     * `Hide` covers the subtree as well: a key below a hidden key is hidden,
     * whatever mode it names itself, because the whole subtree lives in the
     * hidden store of the hive.
     *
     * @param[in] key_path Path of the key relative to the hive root, for
     *                     example `HKEY_CURRENT_USER\Software`.
     * @return The mode of the key. A path which resolves to no listed entry is
     *         reported as `Hide`, which keeps the host registry invisible.
     */
    RegistryKeyIsolation KeyMode(const std::wstring& key_path) const;

    /**
     * @brief Get the listed isolation mode of a value.
     *
     * A value which the table does not list has no mode of its own: it follows
     * the mode of the key which holds it, which is what the caller applies with
     * `appbox::registry::ValueHidesHost()` and
     * `appbox::registry::ValueWritesToHost()`.
     *
     * @param[in] key_path Path of the key which holds the value.
     * @param[in] value_name Name of the value, empty for the default value.
     * @return The listed mode, empty when the value is not listed.
     */
    std::optional<RegistryValueIsolation> ListedValueMode(const std::wstring& key_path,
                                                          const std::wstring& value_name) const;

private:
    /**
     * @brief The listed key modes, ordered by path.
     */
    std::map<std::wstring, RegistryKeyIsolation, IsolationKeyLess> keys_;

    /**
     * @brief The listed value modes, ordered by key path and value name.
     */
    std::map<IsolationValueKey, RegistryValueIsolation, IsolationValueKeyLess> values_;
};

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_ISOLATIONTABLE_HPP
