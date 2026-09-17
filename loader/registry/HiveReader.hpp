#ifndef APPBOX_LOADER_REGISTRY_HIVE_READER_HPP
#define APPBOX_LOADER_REGISTRY_HIVE_READER_HPP

#include <windows.h>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief A single registry value read from the sandbox hive.
 */
struct RegistryValue
{
    /**
     * @brief Value name, empty for the default value of the key.
     */
    std::wstring name;

    /**
     * @brief Registry value type (REG_SZ, REG_DWORD, ...).
     */
    DWORD type;

    /**
     * @brief Raw value bytes, exactly as reported by RegEnumValueW.
     */
    std::vector<BYTE> data;
};

/**
 * @brief Read-only accessor of the sandbox registry hive file.
 *
 * The hive is mounted as a private application hive through RegLoadAppKeyW
 * with KEY_READ. Application hives mount below \REGISTRY\A\{GUID} and are
 * process private, so every key is addressed by a path relative to the
 * returned root handle; the mount point is never opened by path.
 *
 * The mounted view is a snapshot of the hive file at mount time. A sandboxed
 * process flushes its own private mount lazily, so changes which are not on
 * disk yet become visible only after Refresh() remounts the file. The reader
 * never writes, so the shared hive file cannot be damaged by browsing.
 *
 * The host registry is never touched: only the hive file below the overlay
 * directory is accessed, which is exactly the data the sandbox redirects its
 * HKCU writes into.
 */
class HiveReader
{
public:
    HiveReader();
    ~HiveReader();

    HiveReader(const HiveReader&) = delete;
    HiveReader& operator=(const HiveReader&) = delete;
    HiveReader(HiveReader&&) = delete;
    HiveReader& operator=(HiveReader&&) = delete;

    /**
     * @brief Mount the hive file for reading.
     *
     * RegLoadAppKeyW creates the file when it does not exist, but the hive is
     * owned by the sandbox: the loader must not create it. A missing file is
     * therefore reported as IsMissing() instead of being mounted.
     *
     * @param[in] hive_file The DOS path of the hive file.
     * @return true when the hive was mounted, otherwise false.
     */
    bool Open(const std::wstring& hive_file);

    /**
     * @brief Release the mount.
     */
    void Close();

    /**
     * @brief Remount the hive file to pick up flushed changes.
     *
     * The previous mount is released first, so the file is read from its
     * current on-disk state again.
     * @return true when the hive is mounted after the call.
     */
    bool Refresh();

    /**
     * @brief Whether a hive is currently mounted.
     * @return true when the reader holds a root key handle.
     */
    bool IsOpen() const;

    /**
     * @brief Whether the last Open() failed because the file does not exist.
     *
     * The state drives the empty view of the browser: the sandbox has not
     * created the hive yet, which is not an error.
     * @return true when the hive file is missing.
     */
    bool IsMissing() const;

    /**
     * @brief Enumerate the sub key names of a key.
     *
     * @param[in] relative_path The key path relative to the hive root, empty
     *                          for the root itself.
     * @param[out] names The sub key names in enumeration order.
     * @return true on success. A missing key yields false.
     */
    bool EnumSubKeys(const std::wstring& relative_path, std::vector<std::wstring>& names);

    /**
     * @brief Enumerate the values of a key.
     *
     * @param[in] relative_path The key path relative to the hive root, empty
     *                          for the root itself.
     * @param[out] values The values with name, type and raw data.
     * @return true on success. A missing key yields false.
     */
    bool EnumValues(const std::wstring& relative_path, std::vector<RegistryValue>& values);

    /**
     * @brief Whether a key has at least one sub key.
     *
     * The result drives the expand arrows of the browser tree: a key without
     * sub keys must not show a placeholder child.
     * @param[in] relative_path The key path relative to the hive root, empty
     *                          for the root itself.
     * @return true when the key exists and holds at least one sub key.
     */
    bool HasSubKeys(const std::wstring& relative_path);

private:
    HKEY        root_ = nullptr;   /* Root key handle of the private hive mount. */
    bool        missing_ = false;  /* The hive file does not exist. */
    std::wstring file_;            /* DOS path of the hive file. */
};

/**
 * @brief Format a value type as its registry editor style name.
 * @param[in] type The registry value type.
 * @return The display name, for example L"REG_DWORD". Unknown types are
 *         formatted as L"REG_0x...".
 */
std::wstring FormatValueTypeName(DWORD type);

/**
 * @brief Format the data of a value for the list column of the browser.
 *
 * Strings are shown verbatim, DWORD / QWORD as hexadecimal and decimal,
 * MULTI_SZ entries are joined with a space and binary data becomes a byte
 * hex dump. Output longer than max_chars is truncated with an ellipsis.
 *
 * @param[in] value The value to format.
 * @param[in] max_chars The maximum number of characters to produce.
 * @return The formatted data.
 */
std::wstring FormatValueData(const RegistryValue& value, size_t max_chars);

/**
 * @brief Format a byte range as an offset address hex dump.
 *
 * Every line holds the byte offset and up to 16 hexadecimal byte pairs, for
 * example L"00000000  48 65 6c 6c 6f". Used by the value detail dialog.
 *
 * @param[in] data The bytes to dump.
 * @return The multi line dump, empty for empty input.
 */
std::wstring FormatHexDump(const std::vector<BYTE>& data);

/**
 * @brief Read the data of a value as a wide string.
 *
 * The registry stores string values as UTF-16 without a guaranteed trailing
 * NUL, so the length is derived from the byte count.
 * @param[in] value The value to read.
 * @return The string content, empty when the data holds no character.
 */
std::wstring ValueDataAsString(const RegistryValue& value);

} // namespace appbox

#endif // APPBOX_LOADER_REGISTRY_HIVE_READER_HPP
