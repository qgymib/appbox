#include <nlohmann/json.hpp>
#include "WString.hpp"
#include "HiveBuilder.hpp"
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
    L"HKEY_CLASSES_ROOT", L"HKEY_CURRENT_USER",   L"HKEY_LOCAL_MACHINE",
    L"HKEY_USERS",        L"HKEY_CURRENT_CONFIG",
};

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

appbox::test::HiveBuilder::HiveBuilder(const std::filesystem::path& overlay_dir) : overlay_(overlay_dir)
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

void appbox::test::HiveBuilder::SetKeyIsolation(const std::wstring& key_path, appbox::RegistryIsolation isolation)
{
    IsolationEntry entry;
    entry.key_path = key_path;
    entry.isolation = isolation;
    entry.is_value = false;
    isolations_.push_back(std::move(entry));
}

void appbox::test::HiveBuilder::SetValueIsolation(const std::wstring& key_path, const std::wstring& value_name,
                                                  appbox::RegistryIsolation isolation)
{
    IsolationEntry entry;
    entry.key_path = key_path;
    entry.value_name = value_name;
    entry.isolation = isolation;
    entry.is_value = true;
    isolations_.push_back(std::move(entry));
}

bool appbox::test::HiveBuilder::Write(std::string& error)
{
    const auto registry_dir = overlay_ / L"registry";

    std::error_code ec;
    std::filesystem::create_directories(registry_dir, ec);
    if (ec)
    {
        error = "failed to create the registry folder of the overlay";
        return false;
    }

    const auto hive_path = registry_dir / L"user.hiv";
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

    /* Every key and every value of the builder, in the order it was added. */
    for (const auto& entry : entries_)
    {
        if (!written)
        {
            break;
        }

        HKEY  key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(root, entry.key_path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
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
    RegCloseKey(root);

    if (!written)
    {
        RemoveHiveFiles(hive_path);
        return false;
    }

    /* The isolation file lists the modes which were set by the test. */
    nlohmann::json document;
    document[appbox::registry_isolation::kVersionKey] = appbox::registry_isolation::kVersion;
    document[appbox::registry_isolation::kKeysKey] = nlohmann::json::array();
    document[appbox::registry_isolation::kValuesKey] = nlohmann::json::array();

    for (const auto& entry : isolations_)
    {
        nlohmann::json item;
        item[appbox::registry_isolation::kPathKey] = appbox::WideToUTF8(entry.key_path);
        item[appbox::registry_isolation::kIsolationKey] =
            appbox::registry_isolation::IsolationToken(entry.isolation);

        if (!entry.is_value)
        {
            document[appbox::registry_isolation::kKeysKey].push_back(std::move(item));
            continue;
        }

        item[appbox::registry_isolation::kNameKey] = appbox::WideToUTF8(entry.value_name);
        document[appbox::registry_isolation::kValuesKey].push_back(std::move(item));
    }

    const auto text = document.dump(2);
    std::ofstream out(registry_dir / L"isolation.json", std::ios::binary | std::ios::trunc);
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
