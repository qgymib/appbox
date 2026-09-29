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
 * @brief One isolation mode of a registry isolation file.
 *
 * The entry describes a key when `is_value` is false and a value of the key
 * when it is true, which is the shape the document of
 * `common/RegistryIsolation.hpp` uses.
 */
struct RegistryIsolationEntry
{
    /**
     * @brief Path of the key from the hive root, for example
     *        `L"HKEY_CURRENT_USER\\Software\\AppBox"`.
     */
    std::wstring key_path;

    /**
     * @brief Name of the value, empty for a key entry.
     */
    std::wstring value_name;

    /**
     * @brief Whether the entry describes a value instead of a key.
     */
    bool is_value = false;

    /**
     * @brief The mode the file lists for the entry.
     */
    appbox::RegistryIsolation isolation = appbox::RegistryIsolation::WriteCopy;
};

/**
 * @brief Build the text of a registry isolation file.
 *
 * The document is the one the packer writes and the sandbox reads, so a test
 * can hand the modes of a layer to the resources of a case or to a patch
 * package without the packer.
 *
 * @param[in] entries The modes the document lists.
 * @return The UTF-8 text of the document.
 */
std::string BuildRegistryIsolationText(const std::vector<RegistryIsolationEntry>& entries);

/**
 * @brief Builder of the registry artifacts of a test sandbox.
 *
 * The builder writes the hive file and the isolation file of a case directly,
 * so an end-to-end test owns the artifacts the sandbox mounts instead of
 * depending on the packer. Both land in the registry domain of the resources
 * of the case (`<case root>/app/registry`), which is where the loader looks for
 * them: the hive is a read-only resource which the loader seeds into the state
 * directory of the sandbox, the isolation file is handed to the sandbox as it
 * is. The hive holds one sub key per root key of the view, exactly like the
 * hive the packer writes, so the artifacts are interchangeable.
 *
 * The content of the hive and the isolation modes are tracked apart: a mode
 * can be listed for a key which the hive does not hold, which is how a test
 * describes an entry that only exists in the host registry.
 */
class HiveBuilder
{
public:
    /**
     * @brief Create a builder for the resources of a case.
     * @param[in] case_root Root directory of the case, normally the working
     *                      directory.
     */
    explicit HiveBuilder(const std::filesystem::path& case_root);

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
     * @brief Write the hive and the isolation file into the resources.
     *
     * Both files land in the registry domain of the case, which is where the
     * loader looks for them. An existing hive is replaced.
     *
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool Write(std::string& error);

    /**
     * @brief Write the raw text of the isolation file into the resources.
     *
     * A case which pins how the sandbox treats a document it cannot use writes
     * the text itself with this helper, for example a document which is not
     * valid JSON or one of another version. The hive file is not touched, so
     * the helper can rewrite the modes of the resources a case already built.
     *
     * @param[in] text Text to write.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool WriteRawIsolation(const std::string& text, std::string& error);

private:
    /**
     * @brief One key or value of the hive which is being built.
     */
    struct Entry
    {
        std::wstring      key_path;         /* Path of the key from the hive root. */
        std::wstring      value_name;       /* Name of the value, empty for a key entry. */
        DWORD             type = REG_NONE;  /* Type of the value. */
        std::vector<BYTE> data;             /* Data of the value. */
        bool              is_value = false; /* The entry is a value, not a key. */
    };

    /**
     * @brief Registry domain of the resources of the case.
     * @return The path of the folder which holds the hive and the modes.
     */
    std::filesystem::path RegistryDir() const;

    std::filesystem::path               case_root_; /* Root directory of the case. */
    std::vector<Entry>                  entries_;
    std::vector<RegistryIsolationEntry> isolations_;
};

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_HIVE_BUILDER_HPP
