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
 * @brief Whether a key of the model holds an entry whose mode is `Hide`.
 *
 * The test covers the key itself, its values and its whole subtree, so the
 * hidden store of the hive is only created for the keys which need it: a model
 * without a hidden entry produces a hive without a hidden store.
 *
 * @param[in] key The key to test.
 * @return true when the key, one of its values or one of its sub keys is hidden.
 */
bool HoldsHiddenEntry(const appbox::RegistryKeyNode& key)
{
    if (key.isolation == appbox::RegistryKeyIsolation::Hide)
    {
        return true;
    }

    for (const auto& value : key.values)
    {
        if (value.isolation == appbox::RegistryValueIsolation::Hide)
        {
            return true;
        }
    }

    for (const auto& child : key.children)
    {
        if (HoldsHiddenEntry(child))
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Write one value into a key of a hive.
 *
 * The type code of the model is the REG_* code of the API and the data is
 * stored in the raw representation the registry uses, so no conversion is
 * needed. An empty value is written with a null data pointer and a zero size.
 *
 * @param[in] key The key which receives the value.
 * @param[in] value The value to write.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool WriteValue(HKEY key, const appbox::RegistryValueEntry& value, std::string& error)
{
    const BYTE* data = value.data.empty() ? nullptr : value.data.data();
    const LONG  status = RegSetValueExW(key, value.name.c_str(), 0, static_cast<DWORD>(value.type), data,
                                        static_cast<DWORD>(value.data.size()));
    if (status != ERROR_SUCCESS)
    {
        error =
            "failed to write the registry value '" + appbox::WideToUTF8(value.name) + "' (" + ErrorText(status) + ")";
        return false;
    }

    return true;
}

/**
 * @brief Write one key of the model and its subtree into a hive.
 *
 * A key whose mode is `Hide` and everything below it is written into the hidden
 * store of the hive instead of the path of the view (see
 * `common/RegistryIsolation.hpp`), so the packed content of a hidden entry is
 * not part of the view: the sandbox reports the entry as missing until the
 * sandboxed process writes the entry itself, which lands in the view. The store
 * mirrors the structure of the view, so the mirror tree is created along every
 * path which holds a hidden entry.
 *
 * @param[in] view_parent Handle of the key of the view which receives a visible
 *                        key, null while the subtree is hidden.
 * @param[in] mirror_parent Handle of the key of the hidden store which receives
 *                          the mirror of the key.
 * @param[in] node The key to write.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool WriteKey(HKEY view_parent, HKEY mirror_parent, const appbox::RegistryKeyNode& node, std::string& error)
{
    const bool hidden = node.isolation == appbox::RegistryKeyIsolation::Hide;
    const bool mirrored = HoldsHiddenEntry(node);

    HKEY view = nullptr;
    if (!hidden)
    {
        DWORD      disposition = 0;
        const LONG status = RegCreateKeyExW(view_parent, node.name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                            KEY_ALL_ACCESS, nullptr, &view, &disposition);
        if (status != ERROR_SUCCESS)
        {
            error =
                "failed to create the registry key '" + appbox::WideToUTF8(node.name) + "' (" + ErrorText(status) + ")";
            return false;
        }
    }

    HKEY mirror = nullptr;
    if (mirrored)
    {
        DWORD      disposition = 0;
        const LONG status = RegCreateKeyExW(mirror_parent, node.name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                            KEY_ALL_ACCESS, nullptr, &mirror, &disposition);
        if (status != ERROR_SUCCESS)
        {
            error = "failed to create the hidden registry key '" + appbox::WideToUTF8(node.name) + "' (" +
                    ErrorText(status) + ")";
            if (view != nullptr)
            {
                RegCloseKey(view);
            }
            return false;
        }
    }

    for (const auto& value : node.values)
    {
        const bool value_hidden = hidden || value.isolation == appbox::RegistryValueIsolation::Hide;
        HKEY       target = value_hidden ? mirror : view;
        if (!WriteValue(target, value, error))
        {
            if (view != nullptr)
            {
                RegCloseKey(view);
            }
            if (mirror != nullptr)
            {
                RegCloseKey(mirror);
            }
            return false;
        }
    }

    for (const auto& child : node.children)
    {
        if (!WriteKey(view, mirrored ? mirror : mirror_parent, child, error))
        {
            if (view != nullptr)
            {
                RegCloseKey(view);
            }
            if (mirror != nullptr)
            {
                RegCloseKey(mirror);
            }
            return false;
        }
    }

    if (view != nullptr)
    {
        RegCloseKey(view);
    }
    if (mirror != nullptr)
    {
        RegCloseKey(mirror);
    }
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

    /*
     * The hidden store holds the packed content of the entries whose mode is
     * `Hide` and is created only when the model holds one, so a model without a
     * hidden entry produces a hive without a store.
     */
    bool hidden_entries = false;
    for (const auto& child : model.Root().children)
    {
        hidden_entries = hidden_entries || HoldsHiddenEntry(child);
    }

    HKEY store = nullptr;
    bool written = true;
    if (hidden_entries)
    {
        DWORD      disposition = 0;
        const LONG create = RegCreateKeyExW(root, appbox::registry_hidden::kStoreKey, 0, nullptr,
                                            REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &store, &disposition);
        if (create != ERROR_SUCCESS)
        {
            error = "failed to create the hidden registry store (" + ErrorText(create) + ")";
            written = false;
        }
    }

    if (written)
    {
        for (const auto& child : model.Root().children)
        {
            if (!WriteKey(root, store != nullptr ? store : root, child, error))
            {
                written = false;
                break;
            }
        }
    }

    if (store != nullptr)
    {
        RegCloseKey(store);
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
        const auto path =
            std::filesystem::temp_directory_path() / (L"appbox-registry-" + UTF8ToWide(RandomString(16)) + L".hiv");

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
