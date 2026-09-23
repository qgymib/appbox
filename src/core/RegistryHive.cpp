#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <system_error>
#include "Random.hpp"
#include "WString.hpp"
#include "RegistryHive.hpp"

namespace
{

/**
 * @brief Format a Win32 error code for an error description.
 * @param[in] status The error code of the registry API.
 * @return The English error text.
 */
std::string ErrorText(LONG status)
{
    return "error " + std::to_string(status);
}

/**
 * @brief Write one key of the model and its subtree into a hive.
 *
 * @param[in] parent Handle of the parent key inside the hive.
 * @param[in] node The key to write.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool WriteKey(HKEY parent, const appbox::RegistryKeyNode& node, std::string& error)
{
    HKEY  key = nullptr;
    DWORD disposition = 0;
    LONG  status = RegCreateKeyExW(parent, node.name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                                   nullptr, &key, &disposition);
    if (status != ERROR_SUCCESS)
    {
        error = "failed to create the registry key '" + appbox::WideToUTF8(node.name) + "' (" + ErrorText(status) + ")";
        return false;
    }

    for (const auto& value : node.values)
    {
        /*
         * The type code of the model is the REG_* code of the API and the data
         * is stored in the raw representation the registry uses, so no
         * conversion is needed. An empty value is written with a null data
         * pointer and a zero size.
         */
        const BYTE* data = value.data.empty() ? nullptr : value.data.data();
        status = RegSetValueExW(key, value.name.c_str(), 0, static_cast<DWORD>(value.type), data,
                                static_cast<DWORD>(value.data.size()));
        if (status != ERROR_SUCCESS)
        {
            error = "failed to write the registry value '" + appbox::WideToUTF8(value.name) + "' (" + ErrorText(status) +
                    ")";
            RegCloseKey(key);
            return false;
        }
    }

    for (const auto& child : node.children)
    {
        if (!WriteKey(key, child, error))
        {
            RegCloseKey(key);
            return false;
        }
    }

    RegCloseKey(key);
    return true;
}

/**
 * @brief Remove a hive file together with its transaction log files.
 *
 * Mounting a hive creates `<hive>.LOG1` and `<hive>.LOG2` next to it. They
 * belong to the hive and have to be removed with it, otherwise a later mount
 * could replay the log of a file which no longer exists.
 *
 * @param[in] path Path of the hive file.
 */
void RemoveHiveFiles(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);

    const auto text = path.wstring();
    std::filesystem::remove(text + L".LOG1", ec);
    std::filesystem::remove(text + L".LOG2", ec);
}

/**
 * @brief Read a whole file into a byte vector.
 * @param[in] path Path of the file.
 * @param[out] bytes The content of the file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool ReadFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::string& error)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
    {
        error = "failed to read the registry hive '" + appbox::WideToUTF8(path.wstring()) + "'";
        return false;
    }

    const auto size = stream.tellg();
    if (size < 0)
    {
        error = "failed to read the registry hive '" + appbox::WideToUTF8(path.wstring()) + "'";
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        error = "failed to read the registry hive '" + appbox::WideToUTF8(path.wstring()) + "'";
        bytes.clear();
        return false;
    }

    return true;
}

} // namespace

bool appbox::WriteRegistryHive(const RegistryModel& model, const std::wstring& path, std::string& error)
{
    error.clear();

    /*
     * RegLoadAppKeyW mounts the file when it already holds a hive, so an
     * existing file is removed first: the destination has to describe the
     * model and nothing else.
     */
    RemoveHiveFiles(path);

    HKEY root = nullptr;
    /* dwFlags is documented as reserved and must be zero. */
    LONG status = RegLoadAppKeyW(path.c_str(), &root, KEY_ALL_ACCESS, 0, 0);
    if (status != ERROR_SUCCESS)
    {
        error = "failed to create the registry hive '" + WideToUTF8(path) + "' (" + ErrorText(status) + ")";
        return false;
    }

    bool written = true;
    for (const auto& child : model.Root().children)
    {
        if (!WriteKey(root, child, error))
        {
            written = false;
            break;
        }
    }

    if (written)
    {
        /* Flushing makes sure the file is complete before it is closed. */
        status = RegFlushKey(root);
        if (status != ERROR_SUCCESS)
        {
            error = "failed to flush the registry hive '" + WideToUTF8(path) + "' (" + ErrorText(status) + ")";
            written = false;
        }
    }

    RegCloseKey(root);

    if (!written)
    {
        /* Never leave a half written hive behind. */
        RemoveHiveFiles(path);
        return false;
    }

    return true;
}

bool appbox::BuildRegistryHiveBytes(const RegistryModel& model, std::vector<std::uint8_t>& bytes, std::string& error)
{
    bytes.clear();

    try
    {
        const auto path = std::filesystem::temp_directory_path() /
                          (L"appbox-registry-" + UTF8ToWide(RandomString(16)) + L".hiv");

        if (!WriteRegistryHive(model, path.wstring(), error))
        {
            return false;
        }

        const bool read = ReadFileBytes(path, bytes, error);
        RemoveHiveFiles(path);
        return read;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to build the registry hive: ") + e.what();
        return false;
    }
}
