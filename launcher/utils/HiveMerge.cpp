#include "HiveMerge.hpp"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include "RegistryIsolation.hpp"
#include "WString.hpp"

namespace
{

/**
 * @brief Deepest key the merge descends into.
 *
 * The source hive is written by the packer or by the user of the application,
 * so a key which is nested deeper than the budget is refused instead of
 * running the stack of the launcher out.
 */
constexpr std::size_t kMaxDepth = 64;

/**
 * @brief Size of the buffer an enumeration reads a key name into.
 *
 * A key name is limited to 255 characters; the buffer grows when a name of a
 * value turns out to be longer, because a value name may reach 16383
 * characters.
 */
constexpr std::size_t kInitialNameSize = 512;

/**
 * @brief Format a failing registry call.
 * @param[in] what The operation which failed.
 * @param[in] status The status the registry API reported.
 * @return The error description.
 */
std::string FailureText(const char* what, LONG status)
{
    return fmt::format("{}: error {}", what, status);
}

/**
 * @brief Collect the names of the sub keys of a key.
 *
 * The collection runs once, before the keys are created in the target hive: a
 * write disturbs the enumeration of the key it belongs to.
 *
 * @param[in] key The key to enumerate.
 * @param[out] names The names of the sub keys in enumeration order.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectSubKeyNames(HKEY key, std::vector<std::wstring>& names, std::string& error)
{
    std::vector<wchar_t> buffer(kInitialNameSize);

    for (DWORD index = 0;;)
    {
        DWORD      size = static_cast<DWORD>(buffer.size());
        const LONG status = RegEnumKeyExW(key, index, buffer.data(), &size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
        {
            return true;
        }
        if (status == ERROR_MORE_DATA)
        {
            /* The reported size covers the name and its terminator. */
            buffer.resize(static_cast<std::size_t>(size) + 1);
            continue;
        }
        if (status != ERROR_SUCCESS)
        {
            error = FailureText("failed to enumerate the sub keys of the hive", status);
            return false;
        }

        names.emplace_back(buffer.data(), size);
        ++index;
    }
}

/**
 * @brief Collect the names of the values of a key.
 *
 * The default value of a key is enumerated with an empty name and is part of
 * the collection like every other value.
 *
 * @param[in] key The key to enumerate.
 * @param[out] names The names of the values in enumeration order.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool CollectValueNames(HKEY key, std::vector<std::wstring>& names, std::string& error)
{
    std::vector<wchar_t> buffer(kInitialNameSize);

    for (DWORD index = 0;;)
    {
        DWORD      size = static_cast<DWORD>(buffer.size());
        const LONG status = RegEnumValueW(key, index, buffer.data(), &size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
        {
            return true;
        }
        if (status == ERROR_MORE_DATA)
        {
            /* The reported size covers the name and its terminator. */
            buffer.resize(static_cast<std::size_t>(size) + 1);
            continue;
        }
        if (status != ERROR_SUCCESS)
        {
            error = FailureText("failed to enumerate the values of the hive", status);
            return false;
        }

        names.emplace_back(buffer.data(), size);
        ++index;
    }
}

/**
 * @brief Read the type and the raw data of a value.
 * @param[in] key The key which holds the value.
 * @param[in] name Name of the value, empty for the default value.
 * @param[out] type The `REG_*` type code of the value.
 * @param[out] data The raw data of the value.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool ReadValue(HKEY key, const std::wstring& name, DWORD& type, std::vector<BYTE>& data, std::string& error)
{
    type = REG_NONE;
    data.clear();

    /* The first call only asks for the size of the data of the value. */
    DWORD size = 0;
    LONG  status = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA)
    {
        error = fmt::format("failed to read the value '{}' of the hive: error {}", appbox::WideToUTF8(name), status);
        return false;
    }

    data.resize(size);

    DWORD read = size;
    status = RegQueryValueExW(key, name.c_str(), nullptr, &type, data.empty() ? nullptr : data.data(), &read);
    if (status != ERROR_SUCCESS)
    {
        error = fmt::format("failed to read the value '{}' of the hive: error {}", appbox::WideToUTF8(name), status);
        return false;
    }

    data.resize(read);
    return true;
}

/**
 * @brief Write a value with its raw data into a key.
 * @param[in] key The key which receives the value.
 * @param[in] name Name of the value, empty for the default value.
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data The raw data of the value.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool WriteValue(HKEY key, const std::wstring& name, DWORD type, const std::vector<BYTE>& data, std::string& error)
{
    /* The entry point rejects a null data pointer, which an empty value has. */
    static BYTE empty = 0;
    const BYTE* payload = data.empty() ? &empty : data.data();

    const LONG status = RegSetValueExW(key, name.c_str(), 0, type, payload, static_cast<DWORD>(data.size()));
    if (status != ERROR_SUCCESS)
    {
        error = fmt::format("failed to write the value '{}' of the hive: error {}", appbox::WideToUTF8(name), status);
        return false;
    }

    return true;
}

/**
 * @brief Apply the content of one key of the source hive on a key of the target.
 *
 * The values of the source replace the values of the same name of the target
 * and its sub keys are created or merged, so an entry of the target which the
 * source does not name stays in place.
 *
 * @param[in] target The key of the target hive which receives the content.
 * @param[in] source The key of the source hive which is applied on top.
 * @param[in] depth Depth of the key, 0 for the root of the hives.
 * @param[out] error Error description on failure.
 * @return true when the whole content of the key was applied.
 */
bool MergeKey(HKEY target, HKEY source, std::size_t depth, std::string& error)
{
    if (depth > kMaxDepth)
    {
        error = "the hive of the package is nested too deeply";
        return false;
    }

    std::vector<std::wstring> values;
    if (!CollectValueNames(source, values, error))
    {
        return false;
    }

    for (const auto& name : values)
    {
        DWORD             type = REG_NONE;
        std::vector<BYTE> data;
        if (!ReadValue(source, name, type, data, error) || !WriteValue(target, name, type, data, error))
        {
            return false;
        }
    }

    std::vector<std::wstring> keys;
    if (!CollectSubKeyNames(source, keys, error))
    {
        return false;
    }

    for (const auto& name : keys)
    {
        /*
         * The whiteout store of a hive records the deletions of the sandbox
         * and is not part of the content of a layer, so a store a package
         * carries is not applied to the hive the sandbox mounts.
         */
        if (depth == 0 && name == appbox::registry_whiteout::kStoreKey)
        {
            continue;
        }

        HKEY source_child = nullptr;
        LONG status = RegOpenKeyExW(source, name.c_str(), 0, KEY_READ, &source_child);
        if (status != ERROR_SUCCESS)
        {
            error = fmt::format("failed to open the key '{}' of the hive: error {}", appbox::WideToUTF8(name), status);
            return false;
        }

        HKEY  target_child = nullptr;
        DWORD disposition = 0;
        status = RegCreateKeyExW(target, name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                                 &target_child, &disposition);
        if (status != ERROR_SUCCESS)
        {
            RegCloseKey(source_child);
            error =
                fmt::format("failed to create the key '{}' of the hive: error {}", appbox::WideToUTF8(name), status);
            return false;
        }

        const bool merged = MergeKey(target_child, source_child, depth + 1, error);

        RegCloseKey(target_child);
        RegCloseKey(source_child);

        if (!merged)
        {
            return false;
        }
    }

    return true;
}

} // namespace

bool appbox::MergeHiveInto(const std::wstring& target_hive, const std::wstring& source_hive, std::string& error)
{
    error.clear();

    /*
     * Mounting a hive which does not exist creates it, which is what the
     * target wants — the sandbox mounts its hive the same way — and what the
     * source must never do: an empty hive inside the cache entry of a package
     * would look like the content of the package.
     */
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source_hive, ec))
    {
        error = "the hive of the package does not exist";
        return false;
    }

    try
    {
        HKEY       target = nullptr;
        const LONG target_status = RegLoadAppKeyW(target_hive.c_str(), &target, KEY_ALL_ACCESS, 0, 0);
        if (target_status != ERROR_SUCCESS)
        {
            error = fmt::format("failed to mount '{}': error {}", appbox::WideToUTF8(target_hive), target_status);
            return false;
        }

        HKEY       source = nullptr;
        const LONG source_status = RegLoadAppKeyW(source_hive.c_str(), &source, KEY_READ, 0, 0);
        if (source_status != ERROR_SUCCESS)
        {
            RegCloseKey(target);
            error = fmt::format("failed to mount '{}': error {}", appbox::WideToUTF8(source_hive), source_status);
            return false;
        }

        const bool merged = MergeKey(target, source, 0, error);

        /* Closing the root handle unmounts the hive and flushes its content. */
        RegCloseKey(source);
        RegCloseKey(target);

        return merged;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}

namespace
{

/**
 * @brief Split a hive relative key path into its components.
 * @param[in] path The path to split.
 * @param[out] parts The components, empty for an empty path.
 */
void SplitPath(const std::wstring& path, std::vector<std::wstring>& parts)
{
    std::wstring current;
    for (const wchar_t character : path)
    {
        if (character == L'\\' || character == L'/')
        {
            if (!current.empty())
            {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(character);
    }

    if (!current.empty())
    {
        parts.push_back(current);
    }
}

/**
 * @brief Open a key below a root handle, the intermediate keys included.
 *
 * @param[in] root The root handle of the hive.
 * @param[in] parts The components of the path.
 * @param[in] create true creates a key which is missing.
 * @param[out] key The handle of the key. An empty path yields the root handle.
 * @param[out] owned Whether the caller has to close the returned handle.
 * @return true when the key was opened (or created).
 */
bool OpenPath(HKEY root, const std::vector<std::wstring>& parts, bool create, HKEY& key, bool& owned)
{
    key = root;
    owned = false;

    for (const auto& part : parts)
    {
        HKEY       child = nullptr;
        DWORD      disposition = 0;
        const LONG status = create ? RegCreateKeyExW(key, part.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                                     KEY_ALL_ACCESS, nullptr, &child, &disposition)
                                   : RegOpenKeyExW(key, part.c_str(), 0, KEY_ALL_ACCESS, &child);
        if (owned)
        {
            RegCloseKey(key);
            owned = false;
        }

        if (status != ERROR_SUCCESS)
        {
            key = nullptr;
            return false;
        }

        key = child;
        owned = true;
    }

    return true;
}

/**
 * @brief Whether a key holds a value.
 *
 * @param[in] key The key to test.
 * @param[in] name Name of the value, empty for the default value.
 * @return true when the key holds the value.
 */
bool HoldsValue(HKEY key, const std::wstring& name)
{
    DWORD      size = 0;
    const LONG status = RegQueryValueExW(key, name.c_str(), nullptr, nullptr, nullptr, &size);
    return status == ERROR_SUCCESS || status == ERROR_MORE_DATA;
}

/**
 * @brief Copy the values and the sub keys of one key into another key.
 *
 * @param[in] source The key which holds the content.
 * @param[in] destination The key which receives it.
 * @param[in] overwrite true replaces an entry the destination already holds,
 *                      false keeps it.
 * @param[in] depth Depth of the copy.
 * @param[out] error Error description on failure.
 * @return true when the whole content was copied.
 */
bool CopyContent(HKEY source, HKEY destination, bool overwrite, std::size_t depth, std::string& error)
{
    if (depth > kMaxDepth)
    {
        error = "the registry of the layer is nested too deeply";
        return false;
    }

    std::vector<std::wstring> values;
    if (!CollectValueNames(source, values, error))
    {
        return false;
    }

    for (const auto& name : values)
    {
        if (!overwrite && HoldsValue(destination, name))
        {
            /* The destination holds the value: the state of the sandbox wins. */
            continue;
        }

        DWORD             type = REG_NONE;
        std::vector<BYTE> data;
        if (!ReadValue(source, name, type, data, error) || !WriteValue(destination, name, type, data, error))
        {
            return false;
        }
    }

    std::vector<std::wstring> keys;
    if (!CollectSubKeyNames(source, keys, error))
    {
        return false;
    }

    for (const auto& name : keys)
    {
        HKEY source_child = nullptr;
        LONG status = RegOpenKeyExW(source, name.c_str(), 0, KEY_READ, &source_child);
        if (status != ERROR_SUCCESS)
        {
            error = fmt::format("failed to open the key '{}' of the hive: error {}", appbox::WideToUTF8(name), status);
            return false;
        }

        HKEY  destination_child = nullptr;
        DWORD disposition = 0;
        status = RegCreateKeyExW(destination, name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                                 nullptr, &destination_child, &disposition);
        if (status != ERROR_SUCCESS)
        {
            RegCloseKey(source_child);
            error =
                fmt::format("failed to create the key '{}' of the hive: error {}", appbox::WideToUTF8(name), status);
            return false;
        }

        const bool copied = CopyContent(source_child, destination_child, overwrite, depth + 1, error);

        RegCloseKey(destination_child);
        RegCloseKey(source_child);

        if (!copied)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Move a sub key of the view into the hidden store.
 *
 * The content of the key is copied into the store first and the key of the view
 * is removed afterwards, so a failure never loses the content of a layer.
 *
 * @param[in] view_parent The key of the view which holds the sub key.
 * @param[in] store_parent The key of the hidden store which mirrors the parent.
 * @param[in] name Name of the sub key.
 * @param[out] error Error description on failure.
 * @return true when the key is hidden (or the view does not hold it).
 */
bool HideKey(HKEY view_parent, HKEY store_parent, const std::wstring& name, std::string& error)
{
    HKEY view = nullptr;
    if (RegOpenKeyExW(view_parent, name.c_str(), 0, KEY_ALL_ACCESS, &view) != ERROR_SUCCESS)
    {
        /* The view does not hold the key: there is nothing to hide. */
        return true;
    }

    HKEY  store = nullptr;
    DWORD disposition = 0;
    LONG  status = RegCreateKeyExW(store_parent, name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                                   nullptr, &store, &disposition);
    if (status != ERROR_SUCCESS)
    {
        RegCloseKey(view);
        error = fmt::format("failed to create the hidden key '{}': error {}", appbox::WideToUTF8(name), status);
        return false;
    }

    const bool copied = CopyContent(view, store, true, 0, error);
    RegCloseKey(store);
    RegCloseKey(view);

    if (!copied)
    {
        return false;
    }

    if (RegDeleteTreeW(view_parent, name.c_str()) != ERROR_SUCCESS)
    {
        error = fmt::format("failed to hide the key '{}' of the hive", appbox::WideToUTF8(name));
        return false;
    }

    return true;
}

/**
 * @brief Move a sub key of the hidden store into the view.
 *
 * @param[in] view_parent The key of the view which receives the sub key.
 * @param[in] store_parent The key of the hidden store which mirrors the parent.
 * @param[in] name Name of the sub key.
 * @param[out] error Error description on failure.
 * @return true when the key is visible (or the store does not hold it).
 */
bool RevealKey(HKEY view_parent, HKEY store_parent, const std::wstring& name, std::string& error)
{
    HKEY store = nullptr;
    if (RegOpenKeyExW(store_parent, name.c_str(), 0, KEY_ALL_ACCESS, &store) != ERROR_SUCCESS)
    {
        /* The store does not hold the key: there is nothing to reveal. */
        return true;
    }

    HKEY  view = nullptr;
    DWORD disposition = 0;
    LONG  status = RegCreateKeyExW(view_parent, name.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                                   nullptr, &view, &disposition);
    if (status != ERROR_SUCCESS)
    {
        RegCloseKey(store);
        error = fmt::format("failed to create the key '{}' of the hive: error {}", appbox::WideToUTF8(name), status);
        return false;
    }

    const bool copied = CopyContent(store, view, false, 0, error);
    RegCloseKey(view);
    RegCloseKey(store);
    return copied;
}

/**
 * @brief Move a value of the view into the hidden store.
 *
 * @param[in] view The key of the view which holds the value.
 * @param[in] store The key of the hidden store which mirrors the key.
 * @param[in] name Name of the value, empty for the default value.
 * @param[out] error Error description on failure.
 * @return true when the value is hidden (or the view does not hold it).
 */
bool HideValue(HKEY view, HKEY store, const std::wstring& name, std::string& error)
{
    if (!HoldsValue(view, name))
    {
        return true;
    }

    DWORD             type = REG_NONE;
    std::vector<BYTE> data;
    if (!ReadValue(view, name, type, data, error) || !WriteValue(store, name, type, data, error))
    {
        return false;
    }

    RegDeleteValueW(view, name.c_str());
    return true;
}

/**
 * @brief Move a value of the hidden store into the view.
 *
 * @param[in] view The key of the view which receives the value.
 * @param[in] store The key of the hidden store which mirrors the key.
 * @param[in] name Name of the value, empty for the default value.
 * @param[out] error Error description on failure.
 * @return true when the value is visible (or the store does not hold it).
 */
bool RevealValue(HKEY view, HKEY store, const std::wstring& name, std::string& error)
{
    if (HoldsValue(view, name))
    {
        /* The view holds the value: the state of the sandbox wins. */
        return true;
    }

    if (!HoldsValue(store, name))
    {
        return true;
    }

    DWORD             type = REG_NONE;
    std::vector<BYTE> data;
    return ReadValue(store, name, type, data, error) && WriteValue(view, name, type, data, error);
}

} // namespace

bool appbox::EnsureHiveFile(const std::wstring& hive, std::string& error)
{
    error.clear();

    std::error_code ec;
    if (std::filesystem::is_regular_file(hive, ec))
    {
        return true;
    }

    try
    {
        HKEY       root = nullptr;
        const LONG status = RegLoadAppKeyW(hive.c_str(), &root, KEY_ALL_ACCESS, 0, 0);
        if (status != ERROR_SUCCESS)
        {
            error = fmt::format("failed to create '{}': error {}", appbox::WideToUTF8(hive), status);
            return false;
        }

        /* Closing the root handle unmounts the hive and flushes its content. */
        RegCloseKey(root);
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}

bool appbox::ApplyHiveIsolationModes(const std::wstring& hive, const std::vector<registry_isolation::KeyEntry>& keys,
                                     const std::vector<registry_isolation::ValueEntry>& values, std::string& error)
{
    error.clear();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(hive, ec))
    {
        error = "the hive the sandbox mounts does not exist";
        return false;
    }

    try
    {
        HKEY       root = nullptr;
        const LONG status = RegLoadAppKeyW(hive.c_str(), &root, KEY_ALL_ACCESS, 0, 0);
        if (status != ERROR_SUCCESS)
        {
            error = fmt::format("failed to mount '{}': error {}", appbox::WideToUTF8(hive), status);
            return false;
        }

        bool        applied = true;
        std::string detail;

        for (const auto& entry : keys)
        {
            std::vector<std::wstring> parts;
            SplitPath(appbox::UTF8ToWide(entry.path), parts);
            if (parts.empty())
            {
                continue;
            }

            /* The entry itself is the last component; the mirror of the store
             * repeats the path of the view below the store key. */
            const std::wstring        name = parts.back();
            std::vector<std::wstring> parent(parts.begin(), parts.end() - 1);
            std::vector<std::wstring> mirror = parent;
            mirror.insert(mirror.begin(), appbox::registry_hidden::kStoreKey);

            HKEY view_parent = nullptr;
            HKEY store_parent = nullptr;
            bool view_owned = false;
            bool store_owned = false;
            if (!OpenPath(root, parent, true, view_parent, view_owned) ||
                !OpenPath(root, mirror, true, store_parent, store_owned))
            {
                if (view_owned)
                {
                    RegCloseKey(view_parent);
                }
                if (store_owned)
                {
                    RegCloseKey(store_parent);
                }
                detail = fmt::format("failed to open the key '{}' of the hive", entry.path);
                applied = false;
                break;
            }

            applied = entry.isolation == RegistryKeyIsolation::Hide
                          ? HideKey(view_parent, store_parent, name, detail)
                          : RevealKey(view_parent, store_parent, name, detail);

            if (view_owned)
            {
                RegCloseKey(view_parent);
            }
            if (store_owned)
            {
                RegCloseKey(store_parent);
            }

            if (!applied)
            {
                break;
            }
        }

        for (const auto& entry : values)
        {
            if (!applied)
            {
                break;
            }

            std::vector<std::wstring> parts;
            SplitPath(appbox::UTF8ToWide(entry.path), parts);
            if (parts.empty())
            {
                continue;
            }

            std::vector<std::wstring> mirror = parts;
            mirror.insert(mirror.begin(), appbox::registry_hidden::kStoreKey);

            HKEY view_key = nullptr;
            HKEY store_key = nullptr;
            bool view_owned = false;
            bool store_owned = false;
            if (!OpenPath(root, parts, true, view_key, view_owned) ||
                !OpenPath(root, mirror, true, store_key, store_owned))
            {
                if (view_owned)
                {
                    RegCloseKey(view_key);
                }
                if (store_owned)
                {
                    RegCloseKey(store_key);
                }
                detail = fmt::format("failed to open the key '{}' of the hive", entry.path);
                applied = false;
                break;
            }

            const std::wstring name = appbox::UTF8ToWide(entry.name);
            applied = entry.isolation == RegistryValueIsolation::Hide ? HideValue(view_key, store_key, name, detail)
                                                                      : RevealValue(view_key, store_key, name, detail);

            if (view_owned)
            {
                RegCloseKey(view_key);
            }
            if (store_owned)
            {
                RegCloseKey(store_key);
            }
        }

        /* Closing the root handle unmounts the hive and flushes its content. */
        RegCloseKey(root);

        if (!applied)
        {
            error = detail;
        }
        return applied;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}
