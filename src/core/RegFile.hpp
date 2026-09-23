#ifndef APPBOX_PACKER_CORE_REG_FILE_HPP
#define APPBOX_PACKER_CORE_REG_FILE_HPP

#include "RegistryModel.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One operation described by a `.reg` file.
 */
enum class RegFileOperation
{
    AddKey,     ///< `[Key]`, create the key when it does not exist.
    RemoveKey,  ///< `[-Key]`, remove the key with everything below it.
    SetValue,   ///< `"Name"=data`, create or replace a value.
    RemoveValue ///< `"Name"=-`, remove a value.
};

/**
 * @brief One entry of a `.reg` file in file order.
 *
 * The entries are the parsed form of the file: the merge applies them in the
 * order they were read, so a file which creates a key and writes values into
 * it behaves the way the registry editor does.
 */
struct RegFileEntry
{
    /**
     * @brief Operation described by the entry.
     */
    RegFileOperation operation = RegFileOperation::AddKey;

    /**
     * @brief Path of the key the entry addresses.
     *
     * For a value entry this is the key holding the value.
     */
    std::wstring key_path;

    /**
     * @brief Name of the value, empty for the default value of the key.
     */
    std::wstring value_name;

    /**
     * @brief Type of the value.
     */
    RegistryValueType type = RegistryValueType::String;

    /**
     * @brief Raw data of the value.
     */
    std::vector<std::uint8_t> data;

    /**
     * @brief One based line number inside the file, used for diagnostics.
     */
    std::size_t line = 0;
};

/**
 * @brief Parse the text of a `.reg` file.
 *
 * Both formats written by the registry editor are accepted: the version 5
 * format (`Windows Registry Editor Version 5.00`) and the legacy format
 * (`REGEDIT4`). Supported constructs are key sections (`[Key]`), key removal
 * (`[-Key]`), the default value (`@=`), named values (`"Name"=`), value
 * removal (`"Name"=-`), string data, `dword:` data and `hex:` data including
 * the typed forms `hex(0)`, `hex(1)`, `hex(2)`, `hex(3)`, `hex(4)`, `hex(7)`
 * and `hex(b)`.
 *
 * A key path may use the abbreviated root names `HKCR`, `HKCU`, `HKLM`, `HKU`
 * and `HKCC`. Long key lines and hexadecimal blocks may be split over several
 * lines with a trailing backslash.
 *
 * The call is atomic: a syntax error leaves the entry list empty and reports
 * the line it was found on.
 *
 * @param[in] text Text of the file.
 * @param[out] entries The parsed entries in file order.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool ParseRegText(const std::wstring& text, std::vector<RegFileEntry>& entries, std::string& error);

/**
 * @brief Read and parse a `.reg` file.
 *
 * The encoding is detected from the byte order mark: a UTF-16 little endian
 * mark selects UTF-16, a UTF-8 mark selects UTF-8. Without a mark the content
 * is decoded as UTF-8 when it is well formed and as the ANSI code page of the
 * system otherwise, which is how the legacy format was written.
 *
 * @param[in] path Path of the file to read.
 * @param[out] entries The parsed entries in file order.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool LoadRegFile(const std::wstring& path, std::vector<RegFileEntry>& entries, std::string& error);

/**
 * @brief Merge the entries of a `.reg` file into a registry model.
 *
 * The entries are applied in file order: a key which does not exist yet is
 * created together with its intermediate keys, an existing key is merged, an
 * existing value is replaced and a value or key which the file removes is
 * dropped. The isolation modes the user set are never touched, neither for an
 * existing key nor for an existing value.
 *
 * The call is atomic: every entry is validated before the first one is
 * applied, so a rejected file leaves the model unchanged.
 *
 * @param[in,out] model The model to merge into.
 * @param[in] entries The entries to apply.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool MergeRegFile(RegistryModel& model, const std::vector<RegFileEntry>& entries, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_REG_FILE_HPP
