#ifndef APPBOX_TEST_UTILS_HIVE_BUILDER_HPP
#define APPBOX_TEST_UTILS_HIVE_BUILDER_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include "RegistryIsolation.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief Builder of the registry artifacts of a test sandbox.
 *
 * The builder writes the hive file and the isolation file of an overlay
 * directly, so an end-to-end test owns the artifacts the sandbox mounts
 * instead of depending on the packer. The hive holds one sub key per root key
 * of the view, exactly like the hive the packer writes, so the artifacts are
 * interchangeable.
 *
 * The content of the hive and the isolation modes are tracked apart: a mode
 * can be listed for a key which the hive does not hold, which is how a test
 * describes an entry that only exists in the host registry.
 */
class HiveBuilder
{
public:
    /**
     * @brief Create a builder for the overlay directory of a test.
     * @param[in] overlay_dir The overlay directory of the test, for example
     *                        `<cwd>\Upper`.
     */
    explicit HiveBuilder(const std::filesystem::path& overlay_dir);

    /**
     * @brief Add a key to the hive, creating its parents.
     * @param[in] key_path Path of the key from the hive root, for example
     *                     `L"HKEY_CURRENT_USER\Software\\AppBox"`.
     */
    void EnsureKey(const std::wstring& key_path);

    /**
     * @brief Add a value to a key of the hive, creating the key when needed.
     * @param[in] key_path Path of the key from the hive root.
     * @param[in] value_name Name of the value, empty for the default value.
     * @param[in] type The `REG_*` type code of the value.
     * @param[in] data Raw data of the value.
     */
    void SetValue(const std::wstring& key_path, const std::wstring& value_name, DWORD type,
                  const std::vector<BYTE>& data);

    /**
     * @brief List the isolation mode of a key in the isolation file.
     * @param[in] key_path Path of the key from the hive root.
     * @param[in] isolation The mode to list.
     */
    void SetKeyIsolation(const std::wstring& key_path, appbox::RegistryIsolation isolation);

    /**
     * @brief List the isolation mode of a value in the isolation file.
     * @param[in] key_path Path of the key holding the value.
     * @param[in] value_name Name of the value, empty for the default value.
     * @param[in] isolation The mode to list.
     */
    void SetValueIsolation(const std::wstring& key_path, const std::wstring& value_name,
                           appbox::RegistryIsolation isolation);

    /**
     * @brief Write the hive and the isolation file into the overlay.
     *
     * Both files land in the registry folder of the overlay, which is where
     * the loader looks for them. An existing hive is replaced.
     *
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool Write(std::string& error);

private:
    /**
     * @brief One key or value of the hive which is being built.
     */
    struct Entry
    {
        std::wstring      key_path;   /* Path of the key from the hive root. */
        std::wstring      value_name; /* Name of the value, empty for a key entry. */
        DWORD             type = REG_NONE; /* Type of the value. */
        std::vector<BYTE> data;       /* Data of the value. */
        bool              is_value = false; /* The entry is a value, not a key. */
    };

    /**
     * @brief One isolation mode which is listed in the isolation file.
     */
    struct IsolationEntry
    {
        std::wstring              key_path;   /* Path of the key from the hive root. */
        std::wstring              value_name; /* Name of the value, empty for a key entry. */
        appbox::RegistryIsolation isolation = appbox::RegistryIsolation::WriteCopy;
        bool                      is_value = false; /* The entry describes a value. */
    };

    std::filesystem::path      overlay_;
    std::vector<Entry>         entries_;
    std::vector<IsolationEntry> isolations_;
};

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_HIVE_BUILDER_HPP
