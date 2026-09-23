#ifndef APPBOX_PACKER_CORE_REGISTRY_MODEL_HPP
#define APPBOX_PACKER_CORE_REGISTRY_MODEL_HPP

#include "RegistryIsolation.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Label of the virtual registry container shown as the top tree item.
 *
 * The container is not a key of its own: it holds the five root keys of the
 * view and is the parent of every key below them. Its path is the empty
 * string, so `HKEY_CURRENT_USER` is the path of a root key and
 * `HKEY_CURRENT_USER\Software\Vendor` the path of a key below it.
 */
inline constexpr const wchar_t* kRegistryContainerLabel = L"Sandbox Registry";

/**
 * @brief Separator of the registry view paths.
 */
inline constexpr wchar_t kRegistryPathSeparator = L'\\';

/**
 * @brief The root keys offered by the registry view, in display order.
 *
 * The five keys are created by RegistryModel::Reset() and can neither be
 * renamed nor removed. They are also the only entry points an imported `.reg`
 * file may address.
 *
 * @return The names of the root keys.
 */
const std::vector<std::wstring>& RegistryRootKeyNames();

/**
 * @brief Get the display names of the isolation modes.
 *
 * The list is ordered the way the modes are offered by the user interface, so
 * the dropdown of the registry table and the tests share a single source.
 *
 * @return The display names, one per mode.
 */
const std::vector<std::wstring>& RegistryIsolationNames();

/**
 * @brief Get the display name of one isolation mode.
 * @param[in] isolation The isolation mode.
 * @return The display name of the mode.
 */
std::wstring RegistryIsolationName(RegistryIsolation isolation);

/**
 * @brief Registry value types supported by the workspace.
 *
 * The values are the Win32 `REG_*` type codes, so the raw bytes of a value can
 * be handed to the registry API of a future `.hive` writer unchanged.
 */
enum class RegistryValueType : std::uint32_t
{
    None = 0,         ///< REG_NONE, an untyped value.
    String = 1,       ///< REG_SZ.
    ExpandString = 2, ///< REG_EXPAND_SZ.
    Binary = 3,       ///< REG_BINARY.
    Dword = 4,        ///< REG_DWORD.
    MultiString = 7,  ///< REG_MULTI_SZ.
    Qword = 11        ///< REG_QWORD.
};

/**
 * @brief Get the supported value types in display order.
 * @return The value types, one entry per type of the type dropdown.
 */
const std::vector<RegistryValueType>& RegistryValueTypes();

/**
 * @brief Get the registry editor style name of a value type.
 * @param[in] type The value type.
 * @return The display name, for example `L"REG_DWORD"`.
 */
std::wstring RegistryValueTypeName(RegistryValueType type);

/**
 * @brief Resolve a value type from its display name.
 *
 * The comparison ignores the case, so `L"reg_dword"` resolves as well.
 *
 * @param[in] name The display name to resolve.
 * @param[out] out The resolved type when the name is known.
 * @return true when the name names a supported type.
 */
bool ParseRegistryValueType(const std::wstring& name, RegistryValueType& out);

/**
 * @brief Whether a Win32 registry type code is supported by the workspace.
 * @param[in] code The `REG_*` type code.
 * @param[out] out The matching type when the code is supported.
 * @return true when the code names a supported type.
 */
bool ResolveRegistryValueType(std::uint32_t code, RegistryValueType& out);

/**
 * @brief One value of a registry key.
 *
 * The data holds the raw registry bytes of the value, which is the very
 * representation the registry API uses, so no conversion is needed when the
 * model is written into a hive file later on.
 */
struct RegistryValueEntry
{
    /**
     * @brief Name of the value, empty for the default value of the key.
     */
    std::wstring name;

    /**
     * @brief Type of the value.
     */
    RegistryValueType type = RegistryValueType::String;

    /**
     * @brief Raw data of the value.
     */
    std::vector<std::uint8_t> data;

    /**
     * @brief Isolation mode of the value.
     *
     * The mode is always explicit: a value which the user never touched holds
     * the mode of the key it belongs to, see RegistryModel::AddValue() and
     * RegistryModel::SetValue().
     */
    RegistryIsolation isolation = RegistryIsolation::WriteCopy;
};

/**
 * @brief One key of the virtual registry.
 *
 * Every key owns its sub keys and its values, so a key can be moved and
 * destroyed as a whole. The sub keys and the values are kept sorted by name
 * (case insensitive), which is the order the tree and the table show.
 */
struct RegistryKeyNode
{
    /**
     * @brief Name of the key relative to its parent.
     */
    std::wstring name;

    /**
     * @brief Isolation mode of the key.
     *
     * The mode is always explicit: a key which the user never touched holds
     * the mode of its parent, see RegistryModel::AddKey() and
     * RegistryModel::EnsureKey().
     */
    RegistryIsolation isolation = RegistryIsolation::WriteCopy;

    /**
     * @brief Whether the key may be renamed or removed.
     *
     * The virtual container and the five root keys are fixed, so they report
     * false and the model refuses to rename or remove them.
     */
    bool removable = true;

    /**
     * @brief Sub keys of the key, sorted by name.
     */
    std::vector<RegistryKeyNode> children;

    /**
     * @brief Values of the key, sorted by name.
     */
    std::vector<RegistryValueEntry> values;
};

/**
 * @brief One row of the child view of a key.
 *
 * The table below the tree shows the sub keys and the values of the selected
 * key, so a row is either a key (without type and data) or a value.
 */
struct RegistryRow
{
    /**
     * @brief Kind of the row.
     */
    enum class Kind
    {
        Key,   ///< A sub key of the displayed key.
        Value  ///< A value of the displayed key.
    };

    /**
     * @brief Kind of the row.
     */
    Kind kind = Kind::Key;

    /**
     * @brief Path of the key the row belongs to.
     */
    std::wstring key_path;

    /**
     * @brief Name of the sub key, or the name of the value (empty for the
     *        default value).
     */
    std::wstring name;

    /**
     * @brief Isolation mode of the entry behind the row.
     */
    RegistryIsolation isolation = RegistryIsolation::WriteCopy;

    /**
     * @brief Type of the value, only meaningful for value rows.
     */
    RegistryValueType type = RegistryValueType::String;

    /**
     * @brief Raw data of the value, empty for key rows.
     */
    std::vector<std::uint8_t> data;
};

/**
 * @brief Split a registry path into its components.
 *
 * Empty components are dropped, so both separators of a path like
 * `L"HKEY_CURRENT_USER\\Software"` and a path with a trailing separator yield
 * the same component list.
 *
 * @param[in] path The path to split.
 * @return The components in path order, empty for the container path.
 */
std::vector<std::wstring> SplitRegistryPath(const std::wstring& path);

/**
 * @brief Join a parent path and a name into a path.
 * @param[in] parent Path of the parent key, empty for the container.
 * @param[in] name Name of the child key.
 * @return The joined path.
 */
std::wstring JoinRegistryPath(const std::wstring& parent, const std::wstring& name);

/**
 * @brief Get the path of the parent of a key.
 * @param[in] path Path of the key.
 * @return The parent path, empty for a root key and for the container.
 */
std::wstring RegistryParentPath(const std::wstring& path);

/**
 * @brief Get the name of a key inside its parent.
 * @param[in] path Path of the key.
 * @return The last component of the path, empty for the container path.
 */
std::wstring RegistryLeafName(const std::wstring& path);

/**
 * @brief Normalize a path to the separator and shape used by the model.
 * @param[in] path The path to normalize.
 * @return The normalized path, for example `L"HKEY_CURRENT_USER\\Software"`.
 */
std::wstring NormalizeRegistryPath(const std::wstring& path);

/**
 * @brief Compare two registry paths ignoring the case.
 *
 * Registered names are case insensitive by definition, so the model treats
 * `L"HKCU\\Software"` and `L"hkcu\\software"` as the same key.
 *
 * @param[in] left Left path.
 * @param[in] right Right path.
 * @return true when both paths name the same key.
 */
bool RegistryPathEquals(const std::wstring& left, const std::wstring& right);

/**
 * @brief Whether a name may be used as a key or value name.
 * @param[in] name The name to check.
 * @param[in] allow_empty Whether an empty name is accepted, which is the case
 *                        for the default value of a key.
 * @return true when the name is usable.
 */
bool IsValidRegistryName(const std::wstring& name, bool allow_empty);

/**
 * @brief Build the data of a string value.
 *
 * The text is stored as UTF-16 in little endian order, terminated by a NUL
 * character, which is how the registry stores REG_SZ and REG_EXPAND_SZ.
 *
 * @param[in] text The text of the value.
 * @return The raw data of the value.
 */
std::vector<std::uint8_t> RegistryStringData(const std::wstring& text);

/**
 * @brief Read the text of a string value.
 * @param[in] data Raw data of the value.
 * @return The text without its trailing terminators.
 */
std::wstring RegistryStringValue(const std::vector<std::uint8_t>& data);

/**
 * @brief Build the data of a multi string value.
 *
 * Every part is stored as UTF-16 in little endian order, the parts are
 * separated by a single NUL and the whole value is terminated by a second
 * NUL, which is how the registry stores REG_MULTI_SZ.
 *
 * @param[in] parts The strings of the value.
 * @return The raw data of the value.
 */
std::vector<std::uint8_t> RegistryMultiStringData(const std::vector<std::wstring>& parts);

/**
 * @brief Read the strings of a multi string value.
 * @param[in] data Raw data of the value.
 * @return The strings of the value, empty parts dropped.
 */
std::vector<std::wstring> RegistryMultiStringValue(const std::vector<std::uint8_t>& data);

/**
 * @brief Build the data of a DWORD value.
 * @param[in] value The value.
 * @return The raw data of the value.
 */
std::vector<std::uint8_t> RegistryDwordData(std::uint32_t value);

/**
 * @brief Read the value of a DWORD.
 * @param[in] data Raw data of the value.
 * @param[out] out The value when the data holds one.
 * @return true when the data holds exactly one DWORD.
 */
bool RegistryDwordValue(const std::vector<std::uint8_t>& data, std::uint32_t& out);

/**
 * @brief Build the data of a QWORD value.
 * @param[in] value The value.
 * @return The raw data of the value.
 */
std::vector<std::uint8_t> RegistryQwordData(std::uint64_t value);

/**
 * @brief Read the value of a QWORD.
 * @param[in] data Raw data of the value.
 * @param[out] out The value when the data holds one.
 * @return true when the data holds exactly one QWORD.
 */
bool RegistryQwordValue(const std::vector<std::uint8_t>& data, std::uint64_t& out);

/**
 * @brief Parse a hexadecimal byte string.
 *
 * Whitespace is ignored, so the multi line hex blocks of a `.reg` file can be
 * passed as they are.
 *
 * @param[in] text The text to parse.
 * @param[out] out The parsed bytes, empty when the text holds none.
 * @param[out] error Error description when the text is not valid hex.
 * @return true when the text was parsed.
 */
bool ParseRegistryHexText(const std::wstring& text, std::vector<std::uint8_t>& out, std::string& error);

/**
 * @brief Format bytes as a hexadecimal byte string.
 * @param[in] data The bytes to format.
 * @return The hexadecimal text, empty for empty input.
 */
std::wstring FormatRegistryHexText(const std::vector<std::uint8_t>& data);

/**
 * @brief Format the data of a value for the value column of the table.
 *
 * Strings are shown verbatim, DWORD and QWORD values as decimal and
 * hexadecimal numbers, multi strings joined by a separator and every other
 * type as a hexadecimal byte string. The result is truncated to max_chars
 * characters with a trailing ellipsis.
 *
 * @param[in] type Type of the value.
 * @param[in] data Raw data of the value.
 * @param[in] max_chars Maximum number of characters to produce.
 * @return The formatted data.
 */
std::wstring FormatRegistryValueData(RegistryValueType type, const std::vector<std::uint8_t>& data,
                                     std::size_t max_chars);

/**
 * @brief Format the data of a value for the editor of the value dialog.
 *
 * Unlike FormatRegistryValueData(), the multi string entries are separated by
 * line breaks, so the text can be edited in a multi line control.
 *
 * @param[in] type Type of the value.
 * @param[in] data Raw data of the value.
 * @return The editable text of the data.
 */
std::wstring FormatRegistryValueText(RegistryValueType type, const std::vector<std::uint8_t>& data);

/**
 * @brief Parse the text of the value editor into raw value data.
 *
 * Strings are stored as UTF-16, DWORD and QWORD values accept decimal and
 * hexadecimal input (with an optional `0x` prefix), multi strings are split
 * at the line breaks and binary data is parsed as hexadecimal bytes.
 *
 * @param[in] type Type of the value.
 * @param[in] text Text of the editor.
 * @param[out] data The parsed raw data.
 * @param[out] error Error description when the text does not fit the type.
 * @return true when the text was parsed.
 */
bool ParseRegistryValueText(RegistryValueType type, const std::wstring& text,
                            std::vector<std::uint8_t>& data, std::string& error);

/**
 * @brief Editable model of the virtual registry of the packer.
 *
 * The model holds the registry the packaged application will see: a virtual
 * container with the five root keys and, below them, the keys and values
 * imported from `.reg` files or entered by hand. Every key and every value
 * carries its own isolation mode, which defaults to `WriteCopy`.
 *
 * The class holds no wxWidgets dependency, so the tree rules, the isolation
 * rules and the validation are unit testable.
 *
 * Every operation which can fail validates its input first and reports an
 * English error description without changing the model, so a rejected call
 * never leaves the registry half updated.
 */
class RegistryModel
{
public:
    /**
     * @brief Create the model with its five empty root keys.
     */
    RegistryModel();

    /**
     * @brief Drop every key and value below the root keys.
     *
     * The container and the five root keys are recreated in their default
     * state, which is the starting point of an import.
     */
    void Reset();

    /**
     * @brief Get the virtual container which holds the root keys.
     * @return The container node.
     */
    const RegistryKeyNode& Root() const;

    /**
     * @brief Find a key by its path.
     * @param[in] path Path of the key, empty for the container.
     * @return The key, null when it does not exist.
     */
    RegistryKeyNode* FindKey(const std::wstring& path);

    /**
     * @brief Find a key by its path.
     * @param[in] path Path of the key, empty for the container.
     * @return The key, null when it does not exist.
     */
    const RegistryKeyNode* FindKey(const std::wstring& path) const;

    /**
     * @brief Create the keys of a path which do not exist yet.
     *
     * The first component of the path must be one of the root keys; the keys
     * of the remaining components are created when they are missing. The call
     * is a no-op for a path which already exists.
     *
     * @param[in] path Path of the key to create.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool EnsureKey(const std::wstring& path, std::string& error);

    /**
     * @brief Get the rows of the child view of a key.
     *
     * The sub keys come first, then the values; both groups are ordered by
     * name ignoring the case.
     *
     * @param[in] path Path of the displayed key, empty for the container.
     * @return The rows of the key, empty when the key does not exist.
     */
    std::vector<RegistryRow> Rows(const std::wstring& path) const;

    /**
     * @brief Add a sub key to a key.
     * @param[in] parent Path of the parent key, empty for the container.
     * @param[in] name Name of the new key.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddKey(const std::wstring& parent, const std::wstring& name, std::string& error);

    /**
     * @brief Rename a key.
     *
     * The isolation mode of the key and everything below it is untouched.
     *
     * @param[in] path Path of the key to rename.
     * @param[in] new_name New name of the key.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool RenameKey(const std::wstring& path, const std::wstring& new_name, std::string& error);

    /**
     * @brief Remove a key with its sub keys and values.
     * @param[in] path Path of the key to remove.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool RemoveKey(const std::wstring& path, std::string& error);

    /**
     * @brief Add a value to a key.
     * @param[in] parent Path of the key, empty for the container.
     * @param[in] name Name of the value, empty for the default value.
     * @param[in] type Type of the value.
     * @param[in] data Raw data of the value.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddValue(const std::wstring& parent, const std::wstring& name, RegistryValueType type,
                  const std::vector<std::uint8_t>& data, std::string& error);

    /**
     * @brief Create a value or replace its type and data.
     *
     * An existing value keeps its isolation mode, so an import does not
     * discard the modes the user set.
     *
     * @param[in] parent Path of the key, empty for the container.
     * @param[in] name Name of the value, empty for the default value.
     * @param[in] type Type of the value.
     * @param[in] data Raw data of the value.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool SetValue(const std::wstring& parent, const std::wstring& name, RegistryValueType type,
                  const std::vector<std::uint8_t>& data, std::string& error);

    /**
     * @brief Replace the name, the type and the data of a value.
     *
     * The isolation mode of the value is kept, so the dialog of the table does
     * not reset it.
     *
     * @param[in] parent Path of the key holding the value.
     * @param[in] old_name Name of the value to update.
     * @param[in] new_name New name of the value.
     * @param[in] type New type of the value.
     * @param[in] data New raw data of the value.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool UpdateValue(const std::wstring& parent, const std::wstring& old_name,
                     const std::wstring& new_name, RegistryValueType type,
                     const std::vector<std::uint8_t>& data, std::string& error);

    /**
     * @brief Remove a value from a key.
     * @param[in] parent Path of the key holding the value.
     * @param[in] name Name of the value.
     * @return true when the value existed and was removed.
     */
    bool RemoveValue(const std::wstring& parent, const std::wstring& name);

    /**
     * @brief Set the isolation mode of one key.
     *
     * Only the key itself is changed: its sub keys and its values keep their
     * modes, so the change never reaches below the key. Use
     * ApplyIsolationToSubtree() to overwrite a whole subtree on purpose.
     *
     * @param[in] path Path of the key.
     * @param[in] isolation New isolation mode.
     * @return true when the key exists and was updated.
     */
    bool SetKeyIsolation(const std::wstring& path, RegistryIsolation isolation);

    /**
     * @brief Set the isolation mode of one value.
     * @param[in] parent Path of the key holding the value.
     * @param[in] name Name of the value.
     * @param[in] isolation New isolation mode.
     * @return true when the value exists and was updated.
     */
    bool SetValueIsolation(const std::wstring& parent, const std::wstring& name,
                           RegistryIsolation isolation);

    /**
     * @brief Overwrite the isolation mode of a key and of everything below it.
     *
     * The key and every sub key of every level are set to the new mode, no
     * matter which mode they held before. When include_values is true, the
     * values of the key and the values of its whole subtree are overwritten as
     * well; otherwise every value keeps its own mode.
     *
     * @param[in] path Path of the key.
     * @param[in] isolation New isolation mode.
     * @param[in] include_values Whether the values below the key are overwritten too.
     * @return true when the key exists and was updated.
     */
    bool ApplyIsolationToSubtree(const std::wstring& path, RegistryIsolation isolation, bool include_values);

private:
    /**
     * @brief Find the position of a child of a key.
     * @param[in] parent The parent key.
     * @param[in] name Name of the child.
     * @return The index of the child, -1 when it does not exist.
     */
    static std::ptrdiff_t ChildIndex(const RegistryKeyNode& parent, const std::wstring& name);

    /**
     * @brief Find the position of a value of a key.
     * @param[in] parent The parent key.
     * @param[in] name Name of the value.
     * @return The index of the value, -1 when it does not exist.
     */
    static std::ptrdiff_t ValueIndex(const RegistryKeyNode& parent, const std::wstring& name);

    /**
     * @brief Restore the sort order of the children and values of a key.
     * @param[in,out] key The key to sort.
     */
    static void SortKey(RegistryKeyNode& key);

    /**
     * @brief Overwrite the isolation mode of a key and of everything below it.
     * @param[in,out] key The key to update.
     * @param[in] isolation New isolation mode.
     * @param[in] include_values Whether the values below the key are overwritten too.
     */
    static void ApplyIsolation(RegistryKeyNode& key, RegistryIsolation isolation, bool include_values);

    /**
     * @brief The virtual container holding the five root keys.
     */
    RegistryKeyNode root_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_REGISTRY_MODEL_HPP
