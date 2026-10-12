#include <nlohmann/json.hpp>
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include "HiveBuilder.hpp"
#include <cwctype>
#include <fstream>
#include <system_error>
#include <utility>

namespace
{

/**
 * @brief The root keys of the view, in the order the hive carries them.
 *
 * The list mirrors `appbox::registry::HiveRootKeyNames()` of the sandbox, so a
 * hive which is built by a test has the layout the sandbox expects.
 */
const wchar_t* const kRootKeyNames[] = {
    L"HKEY_CLASSES_ROOT", L"HKEY_CURRENT_USER", L"HKEY_LOCAL_MACHINE", L"HKEY_USERS", L"HKEY_CURRENT_CONFIG",
};

/**
 * @brief Compare two names ignoring the case.
 * @param[in] left Left name.
 * @param[in] right Right name.
 * @return true when both names are equal ignoring the case.
 */
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towlower(left[index]) != std::towlower(right[index]))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether a key path is a prefix of another key path.
 *
 * The comparison ignores the case and the separator after the prefix, so
 * `L"A\\B"` is a prefix of `L"A\\B\\C"` and of `L"A\\B"` itself.
 *
 * @param[in] prefix The prefix to test.
 * @param[in] path The path to test.
 * @return true when prefix names the path or one of its ancestors.
 */
bool IsPathPrefix(const std::wstring& prefix, const std::wstring& path)
{
    if (prefix.size() > path.size())
    {
        return false;
    }

    if (!EqualsIgnoreCase(prefix, path.substr(0, prefix.size())))
    {
        return false;
    }

    return prefix.size() == path.size() || path[prefix.size()] == L'\\' || path[prefix.size()] == L'/';
}

/**
 * @brief Remove a hive file together with its transaction log files.
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

} // namespace

std::string appbox::test::BuildRegistryIsolationText(const std::vector<RegistryKeyIsolationEntry>&   keys,
                                                     const std::vector<RegistryValueIsolationEntry>& values)
{
    appbox::registry_isolation::Document document;

    for (const auto& entry : keys)
    {
        appbox::registry_isolation::KeyEntry item;
        item.path = appbox::WideToUTF8(entry.key_path);
        item.isolation = entry.isolation;
        document.keys.push_back(std::move(item));
    }

    for (const auto& entry : values)
    {
        appbox::registry_isolation::ValueEntry item;
        item.path = appbox::WideToUTF8(entry.key_path);
        item.name = appbox::WideToUTF8(entry.value_name);
        item.isolation = entry.isolation;
        document.values.push_back(std::move(item));
    }

    return nlohmann::json(document).dump(2);
}

appbox::test::HiveBuilder::HiveBuilder(const std::filesystem::path& case_root) : case_root_(case_root)
{
}

void appbox::test::HiveBuilder::EnsureKey(const std::wstring& key_path)
{
    Entry entry;
    entry.key_path = key_path;
    entry.is_value = false;
    entries_.push_back(std::move(entry));
}

void appbox::test::HiveBuilder::SetValue(const std::wstring& key_path, const std::wstring& value_name, DWORD type,
                                         const std::vector<BYTE>& data)
{
    Entry entry;
    entry.key_path = key_path;
    entry.value_name = value_name;
    entry.type = type;
    entry.data = data;
    entry.is_value = true;
    entries_.push_back(std::move(entry));
}

void appbox::test::HiveBuilder::SetKeyIsolation(const std::wstring& key_path, appbox::RegistryKeyIsolation isolation)
{
    RegistryKeyIsolationEntry entry;
    entry.key_path = key_path;
    entry.isolation = isolation;
    key_isolations_.push_back(std::move(entry));
}

void appbox::test::HiveBuilder::SetValueIsolation(const std::wstring& key_path, const std::wstring& value_name,
                                                  appbox::RegistryValueIsolation isolation)
{
    RegistryValueIsolationEntry entry;
    entry.key_path = key_path;
    entry.value_name = value_name;
    entry.isolation = isolation;
    value_isolations_.push_back(std::move(entry));
}

bool appbox::test::HiveBuilder::KeyIsHidden(const std::wstring& key_path) const
{
    for (const auto& entry : key_isolations_)
    {
        if (entry.isolation == appbox::RegistryKeyIsolation::Hide && IsPathPrefix(entry.key_path, key_path))
        {
            return true;
        }
    }

    return false;
}

bool appbox::test::HiveBuilder::ValueIsHidden(const std::wstring& key_path, const std::wstring& value_name) const
{
    if (KeyIsHidden(key_path))
    {
        return true;
    }

    for (const auto& entry : value_isolations_)
    {
        if (entry.isolation == appbox::RegistryValueIsolation::Hide && EqualsIgnoreCase(entry.key_path, key_path) &&
            EqualsIgnoreCase(entry.value_name, value_name))
        {
            return true;
        }
    }

    return false;
}

std::filesystem::path appbox::test::HiveBuilder::RegistryDir() const
{
    return case_root_ / appbox::layout::kAppDirNameW / appbox::layout::kRegistryDirNameW;
}

bool appbox::test::HiveBuilder::Write(std::string& error)
{
    const auto registry_dir = RegistryDir();

    std::error_code ec;
    std::filesystem::create_directories(registry_dir, ec);
    if (ec)
    {
        error = "failed to create the registry folder of the resources";
        return false;
    }

    const auto hive_path = registry_dir / appbox::layout::kRegistryHiveFileNameW;
    RemoveHiveFiles(hive_path);

    HKEY root = nullptr;
    if (RegLoadAppKeyW(hive_path.wstring().c_str(), &root, KEY_ALL_ACCESS, 0, 0) != ERROR_SUCCESS)
    {
        error = "failed to create the test hive";
        return false;
    }

    bool written = true;

    /* The five root keys of the view, like a hive the packer wrote. */
    for (const auto* name : kRootKeyNames)
    {
        HKEY  key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(root, name, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &key,
                            &disposition) != ERROR_SUCCESS)
        {
            error = "failed to create a root key of the test hive";
            written = false;
            break;
        }
        RegCloseKey(key);
    }

    /*
     * The hidden store holds the content of the entries whose mode is `Hide`,
     * which is what the packer does: the view does not carry the packed content
     * of a hidden entry, so the sandbox reports the entry as missing until the
     * sandboxed process writes it. The store is created only when a hidden entry
     * exists, exactly like the packer creates it.
     */
    bool hidden_entries = false;
    for (const auto& entry : entries_)
    {
        hidden_entries = hidden_entries || (entry.is_value ? ValueIsHidden(entry.key_path, entry.value_name)
                                                           : KeyIsHidden(entry.key_path));
    }

    HKEY store = nullptr;
    if (written && hidden_entries)
    {
        DWORD disposition = 0;
        if (RegCreateKeyExW(root, appbox::registry_hidden::kStoreKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                            KEY_ALL_ACCESS, nullptr, &store, &disposition) != ERROR_SUCCESS)
        {
            error = "failed to create the hidden store of the test hive";
            written = false;
        }
    }

    /* Every key and every value of the builder, in the order it was added. */
    for (const auto& entry : entries_)
    {
        if (!written)
        {
            break;
        }

        const bool hidden =
            entry.is_value ? ValueIsHidden(entry.key_path, entry.value_name) : KeyIsHidden(entry.key_path);
        HKEY parent = hidden ? store : root;
        if (parent == nullptr)
        {
            continue;
        }

        HKEY  key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(parent, entry.key_path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                            nullptr, &key, &disposition) != ERROR_SUCCESS)
        {
            error = "failed to create a key of the test hive";
            written = false;
            break;
        }

        if (entry.is_value)
        {
            const BYTE* data = entry.data.empty() ? nullptr : entry.data.data();
            if (RegSetValueExW(key, entry.value_name.c_str(), 0, entry.type, data,
                               static_cast<DWORD>(entry.data.size())) != ERROR_SUCCESS)
            {
                error = "failed to write a value of the test hive";
                written = false;
            }
        }

        RegCloseKey(key);
    }

    if (written)
    {
        RegFlushKey(root);
    }
    if (store != nullptr)
    {
        RegCloseKey(store);
    }
    RegCloseKey(root);

    if (!written)
    {
        RemoveHiveFiles(hive_path);
        return false;
    }

    /*
     * The isolation file lists the modes which were set by the test. The five
     * root keys of the view are always listed: the sandbox refuses a file which
     * does not name every one of them, and a root key the test did not set keeps
     * the mode of the builder, see the class comment of HiveBuilder.
     */
    std::vector<RegistryKeyIsolationEntry> keys;
    for (const auto* name : kRootKeyNames)
    {
        RegistryKeyIsolationEntry entry;
        entry.key_path = name;
        entry.isolation = appbox::RegistryKeyIsolation::WriteCopy;
        keys.push_back(std::move(entry));
    }

    for (const auto& entry : key_isolations_)
    {
        bool listed = false;
        for (auto& key : keys)
        {
            if (EqualsIgnoreCase(key.key_path, entry.key_path))
            {
                key.isolation = entry.isolation;
                listed = true;
                break;
            }
        }

        if (!listed)
        {
            keys.push_back(entry);
        }
    }

    const auto    text = appbox::test::BuildRegistryIsolationText(keys, value_isolations_);
    std::ofstream out(registry_dir / appbox::layout::kIsolationFileNameW, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        error = "failed to create the isolation file of the test";
        return false;
    }

    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out.good())
    {
        error = "failed to write the isolation file of the test";
        return false;
    }

    return true;
}

bool appbox::test::HiveBuilder::WriteRawIsolation(const std::string& text, std::string& error)
{
    const auto registry_dir = RegistryDir();

    std::error_code ec;
    std::filesystem::create_directories(registry_dir, ec);
    if (ec)
    {
        error = "failed to create the registry folder of the resources";
        return false;
    }

    std::ofstream out(registry_dir / appbox::layout::kIsolationFileNameW, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        error = "failed to create the isolation file of the test";
        return false;
    }

    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out.good())
    {
        error = "failed to write the isolation file of the test";
        return false;
    }

    return true;
}
