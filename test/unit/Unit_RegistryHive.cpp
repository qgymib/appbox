#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include "src/core/RegistryHive.hpp"
#include "src/core/RegistryIsolationFile.hpp"
#include "registry/IsolationTable.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Generate a unique name fragment for temporary folders.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

/**
 * @brief RAII helper creating a unique folder below the temp directory.
 */
class TempDir
{
public:
    TempDir()
    {
        path_ = std::filesystem::temp_directory_path() / (L"appbox-hive-test-" + UniqueFragment());
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    /**
     * @brief Get the folder path.
     * @return The folder path.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief RAII owner of a hive mount.
 */
class HiveMount
{
public:
    ~HiveMount()
    {
        if (root_ != nullptr)
        {
            RegCloseKey(root_);
        }
    }

    /**
     * @brief Mount a hive file for reading.
     * @param[in] path Path of the hive file.
     * @return true on success.
     */
    bool Open(const std::filesystem::path& path)
    {
        return RegLoadAppKeyW(path.wstring().c_str(), &root_, KEY_READ, 0, 0) == ERROR_SUCCESS;
    }

    /**
     * @brief The root handle of the mount.
     * @return The root handle.
     */
    HKEY get() const
    {
        return root_;
    }

private:
    HKEY root_ = nullptr;
};

/**
 * @brief One value read back from a hive.
 */
struct ValueProbe
{
    DWORD                type = 0;
    std::vector<uint8_t> data;
};

/**
 * @brief Read a value of a key.
 * @param[in] key Key handle.
 * @param[in] name Name of the value.
 * @param[out] out The type and the data of the value.
 * @return true when the value was read.
 */
bool ReadValue(HKEY key, const wchar_t* name, ValueProbe& out)
{
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &out.type, nullptr, &size) != ERROR_SUCCESS)
    {
        return false;
    }

    out.data.resize(size);
    if (size == 0)
    {
        return true;
    }
    return RegQueryValueExW(key, name, nullptr, &out.type, out.data.data(), &size) == ERROR_SUCCESS;
}

/**
 * @brief Build a model which uses every supported value type and two roots.
 * @param[out] model The model to fill.
 */
void FillModel(appbox::RegistryModel& model)
{
    std::string error;
    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Software\\AppBox\\Values", error)) << error;
    ASSERT_TRUE(model.EnsureKey(L"HKEY_LOCAL_MACHINE\\Software\\AppBox\\Deep\\Key", error)) << error;

    const auto parent = L"HKEY_CURRENT_USER\\Software\\AppBox\\Values";
    ASSERT_TRUE(model.SetValue(parent, L"None", appbox::RegistryValueType::None, { 0x01, 0x02 }, error)) << error;
    ASSERT_TRUE(model.SetValue(parent, L"String", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"AppBox"), error)) << error;
    ASSERT_TRUE(model.SetValue(parent, L"Expand", appbox::RegistryValueType::ExpandString,
                               appbox::RegistryStringData(L"%SystemRoot%\\AppBox"), error)) << error;
    ASSERT_TRUE(model.SetValue(parent, L"Binary", appbox::RegistryValueType::Binary, { 0xDE, 0xAD, 0xBE, 0xEF },
                               error)) << error;
    ASSERT_TRUE(model.SetValue(parent, L"Dword", appbox::RegistryValueType::Dword, appbox::RegistryDwordData(0x1234),
                               error)) << error;
    ASSERT_TRUE(model.SetValue(parent, L"Multi", appbox::RegistryValueType::MultiString,
                               appbox::RegistryMultiStringData({ L"one", L"two" }), error)) << error;
    ASSERT_TRUE(model.SetValue(parent, L"Qword", appbox::RegistryValueType::Qword,
                               appbox::RegistryQwordData(0x1122334455667788ULL), error)) << error;

    /* The default value of a key is stored with an empty name. */
    ASSERT_TRUE(model.SetValue(L"HKEY_LOCAL_MACHINE\\Software\\AppBox", L"", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"default"), error)) << error;
}

} // namespace

/**
 * @brief The hive of a model holds the five root keys and every value of the
 *        model with its type and its bytes.
 */
TEST(UnitRegistryHive, WriteAndMount)
{
    TempDir                 temp;
    appbox::RegistryModel   model;
    FillModel(model);

    const auto hive = temp.Get() / L"user.hiv";
    std::string error;
    ASSERT_TRUE(appbox::WriteRegistryHive(model, hive.wstring(), error)) << error;
    ASSERT_TRUE(std::filesystem::exists(hive));

    HiveMount mount;
    ASSERT_TRUE(mount.Open(hive));

    /* The hive holds one sub key per root key of the model. */
    DWORD sub_keys = 0;
    ASSERT_EQ(RegQueryInfoKeyW(mount.get(), nullptr, nullptr, nullptr, &sub_keys, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr, nullptr),
              ERROR_SUCCESS);
    ASSERT_EQ(sub_keys, 5u);

    const auto& root_names = appbox::RegistryRootKeyNames();
    for (const auto& name : root_names)
    {
        HKEY key = nullptr;
        ASSERT_EQ(RegOpenKeyExW(mount.get(), name.c_str(), 0, KEY_READ, &key), ERROR_SUCCESS) << name;
        RegCloseKey(key);
    }

    /* Every value of the model survives with its type and its bytes. */
    HKEY key = nullptr;
    ASSERT_EQ(RegOpenKeyExW(mount.get(), L"HKEY_CURRENT_USER\\Software\\AppBox\\Values", 0, KEY_READ, &key),
              ERROR_SUCCESS);

    ValueProbe probe;
    ASSERT_TRUE(ReadValue(key, L"None", probe));
    ASSERT_EQ(probe.type, static_cast<DWORD>(appbox::RegistryValueType::None));
    ASSERT_EQ(probe.data, (std::vector<uint8_t>{ 0x01, 0x02 }));

    ASSERT_TRUE(ReadValue(key, L"String", probe));
    ASSERT_EQ(probe.type, static_cast<DWORD>(appbox::RegistryValueType::String));
    ASSERT_EQ(appbox::RegistryStringValue(probe.data), L"AppBox");

    ASSERT_TRUE(ReadValue(key, L"Expand", probe));
    ASSERT_EQ(probe.type, static_cast<DWORD>(appbox::RegistryValueType::ExpandString));
    ASSERT_EQ(appbox::RegistryStringValue(probe.data), L"%SystemRoot%\\AppBox");

    ASSERT_TRUE(ReadValue(key, L"Binary", probe));
    ASSERT_EQ(probe.type, static_cast<DWORD>(appbox::RegistryValueType::Binary));
    ASSERT_EQ(probe.data, (std::vector<uint8_t>{ 0xDE, 0xAD, 0xBE, 0xEF }));

    ASSERT_TRUE(ReadValue(key, L"Dword", probe));
    ASSERT_EQ(probe.type, static_cast<DWORD>(appbox::RegistryValueType::Dword));
    uint32_t dword = 0;
    ASSERT_TRUE(appbox::RegistryDwordValue(probe.data, dword));
    ASSERT_EQ(dword, 0x1234u);

    ASSERT_TRUE(ReadValue(key, L"Multi", probe));
    ASSERT_EQ(probe.type, static_cast<DWORD>(appbox::RegistryValueType::MultiString));
    ASSERT_EQ(appbox::RegistryMultiStringValue(probe.data), (std::vector<std::wstring>{ L"one", L"two" }));

    ASSERT_TRUE(ReadValue(key, L"Qword", probe));
    ASSERT_EQ(probe.type, static_cast<DWORD>(appbox::RegistryValueType::Qword));
    uint64_t qword = 0;
    ASSERT_TRUE(appbox::RegistryQwordValue(probe.data, qword));
    ASSERT_EQ(qword, 0x1122334455667788ULL);

    RegCloseKey(key);

    /* A nested key and its default value are stored as well. */
    ASSERT_EQ(RegOpenKeyExW(mount.get(), L"HKEY_LOCAL_MACHINE\\Software\\AppBox", 0, KEY_READ, &key), ERROR_SUCCESS);
    ASSERT_TRUE(ReadValue(key, L"", probe));
    ASSERT_EQ(appbox::RegistryStringValue(probe.data), L"default");
    RegCloseKey(key);

    ASSERT_EQ(RegOpenKeyExW(mount.get(), L"HKEY_LOCAL_MACHINE\\Software\\AppBox\\Deep\\Key", 0, KEY_READ, &key),
              ERROR_SUCCESS);
    RegCloseKey(key);
}

/**
 * @brief An empty model still produces a hive with the five root keys, and the
 *        bytes of the hive can be built in memory.
 */
TEST(UnitRegistryHive, BuildBytes)
{
    appbox::RegistryModel model;
    std::vector<uint8_t>  bytes;
    std::string           error;
    ASSERT_TRUE(appbox::BuildRegistryHiveBytes(model, bytes, error)) << error;
    ASSERT_GT(bytes.size(), 0u);

    /* A hive file starts with the "regf" signature. */
    ASSERT_GE(bytes.size(), 4u);
    ASSERT_EQ(std::string(reinterpret_cast<const char*>(bytes.data()), 4), "regf");

    /* The temporary file is removed again. */
    std::size_t leftovers = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path()))
    {
        if (entry.path().filename().wstring().rfind(L"appbox-registry-", 0) == 0)
        {
            ++leftovers;
        }
    }
    ASSERT_EQ(leftovers, 0u);
}

/**
 * @brief The hive of an existing file is replaced, not merged.
 */
TEST(UnitRegistryHive, ReplacesAnExistingHive)
{
    TempDir               temp;
    appbox::RegistryModel model;
    std::string           error;
    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Software\\First", error)) << error;

    const auto hive = temp.Get() / L"user.hiv";
    ASSERT_TRUE(appbox::WriteRegistryHive(model, hive.wstring(), error)) << error;

    appbox::RegistryModel second;
    ASSERT_TRUE(second.EnsureKey(L"HKEY_CURRENT_USER\\Software\\Second", error)) << error;
    ASSERT_TRUE(appbox::WriteRegistryHive(second, hive.wstring(), error)) << error;

    HiveMount mount;
    ASSERT_TRUE(mount.Open(hive));

    /* Every handle is closed before the assertions, so a failing expectation
     * cannot keep the hive locked and block the cleanup of the folder. */
    HKEY second_key = nullptr;
    const LONG second_status =
        RegOpenKeyExW(mount.get(), L"HKEY_CURRENT_USER\\Software\\Second", 0, KEY_READ, &second_key);
    if (second_key != nullptr)
    {
        RegCloseKey(second_key);
    }

    HKEY first_key = nullptr;
    const LONG first_status =
        RegOpenKeyExW(mount.get(), L"HKEY_CURRENT_USER\\Software\\First", 0, KEY_READ, &first_key);
    if (first_key != nullptr)
    {
        RegCloseKey(first_key);
    }

    ASSERT_EQ(second_status, ERROR_SUCCESS);
    ASSERT_EQ(first_status, ERROR_FILE_NOT_FOUND);
}

/**
 * @brief A destination which cannot be created is reported and leaves no file.
 */
TEST(UnitRegistryHive, WriteFailure)
{
    TempDir               temp;
    appbox::RegistryModel model;
    std::string           error;

    const auto hive = temp.Get() / L"missing" / L"user.hiv";
    ASSERT_FALSE(appbox::WriteRegistryHive(model, hive.wstring(), error));
    ASSERT_FALSE(error.empty());
    ASSERT_FALSE(std::filesystem::exists(hive));
}

/**
 * @brief The isolation file lists every key and every value with its mode.
 */
TEST(UnitRegistryHive, IsolationFile)
{
    appbox::RegistryModel model;
    std::string           error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Software\\Vendor", error)) << error;
    ASSERT_TRUE(model.EnsureKey(L"HKEY_LOCAL_MACHINE\\Software\\AppBox", error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Server", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"host"), error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Other", appbox::RegistryValueType::Dword,
                               appbox::RegistryDwordData(1), error)) << error;

    ASSERT_TRUE(model.SetKeyIsolation(L"HKEY_CURRENT_USER\\Software\\Vendor", appbox::RegistryIsolation::Full));
    ASSERT_TRUE(model.SetValueIsolation(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Server",
                                        appbox::RegistryIsolation::Hide));

    std::string text;
    ASSERT_TRUE(appbox::BuildRegistryIsolationFile(model, text, error)) << error;

    const auto document = nlohmann::json::parse(text);
    ASSERT_EQ(document["version"].get<int>(), 1);

    /* Every key of the model is listed, the five root keys included. */
    const auto& keys = document["keys"];
    ASSERT_EQ(keys.size(), 9u);
    ASSERT_EQ(keys[0]["path"].get<std::string>(), "HKEY_CLASSES_ROOT");
    ASSERT_EQ(keys[0]["isolation"].get<std::string>(), "write_copy");
    ASSERT_EQ(keys[3]["path"].get<std::string>(), "HKEY_CURRENT_USER\\Software\\Vendor");
    ASSERT_EQ(keys[3]["isolation"].get<std::string>(), "full");

    /* The values are listed as well, in the name order of their key. */
    const auto& values = document["values"];
    ASSERT_EQ(values.size(), 2u);
    ASSERT_EQ(values[0]["path"].get<std::string>(), "HKEY_CURRENT_USER\\Software\\Vendor");
    ASSERT_EQ(values[0]["name"].get<std::string>(), "Other");
    ASSERT_EQ(values[0]["isolation"].get<std::string>(), "write_copy");
    ASSERT_EQ(values[1]["path"].get<std::string>(), "HKEY_CURRENT_USER\\Software\\Vendor");
    ASSERT_EQ(values[1]["name"].get<std::string>(), "Server");
    ASSERT_EQ(values[1]["isolation"].get<std::string>(), "hide");

    /* The document is accepted by the sandbox side of the schema. */
    appbox::registry::IsolationTable table;
    ASSERT_TRUE(table.Parse(text, error)) << error;
    ASSERT_EQ(table.KeyMode(L"HKEY_CURRENT_USER\\Software\\Vendor"), appbox::RegistryIsolation::Full);
    ASSERT_EQ(table.ValueMode(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Server"), appbox::RegistryIsolation::Hide);
    ASSERT_EQ(table.KeyMode(L"HKEY_LOCAL_MACHINE\\Software\\AppBox"), appbox::RegistryIsolation::WriteCopy);
}

/**
 * @brief A model which was never touched lists its root keys and no value.
 */
TEST(UnitRegistryHive, IsolationFileOfADefaultModel)
{
    appbox::RegistryModel model;
    std::string           text;
    std::string           error;
    ASSERT_TRUE(appbox::BuildRegistryIsolationFile(model, text, error)) << error;

    const auto document = nlohmann::json::parse(text);
    ASSERT_EQ(document["version"].get<int>(), 1);

    const auto& keys = document["keys"];
    ASSERT_EQ(keys.size(), appbox::RegistryRootKeyNames().size());
    ASSERT_EQ(keys[0]["path"].get<std::string>(), "HKEY_CLASSES_ROOT");
    ASSERT_EQ(keys[0]["isolation"].get<std::string>(), "write_copy");
    ASSERT_TRUE(document["values"].empty());
}
