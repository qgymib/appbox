#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include "RegistryIsolation.hpp"
#include "utils/HiveMerge.hpp"
#include "WString.hpp"

namespace
{

/**
 * @brief Path of a hive file below the temporary directory of the machine.
 *
 * The unit tests run sequentially, so a fixed name with a per test cleanup is
 * enough.
 *
 * @param[in] name Name of the file.
 * @return The DOS path of the hive file.
 */
std::wstring HivePath(const wchar_t* name)
{
    return (std::filesystem::temp_directory_path() / name).wstring();
}

/**
 * @brief Remove a hive file together with its transaction log files.
 * @param[in] path Path of the hive file.
 */
void RemoveHiveFiles(const std::wstring& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + L".LOG1", ec);
    std::filesystem::remove(path + L".LOG2", ec);
}

/**
 * @brief A hive file which is mounted for the duration of a scope.
 *
 * Closing the root handle unmounts the hive and flushes its content, which is
 * what the sandboxed process does when it exits.
 */
class MountedHive
{
public:
    /**
     * @brief Mount a hive file.
     * @param[in] path DOS path of the hive file, created when it is missing.
     * @param[in] access Access mask of the mount.
     */
    explicit MountedHive(const std::wstring& path, REGSAM access = KEY_ALL_ACCESS)
    {
        if (RegLoadAppKeyW(path.c_str(), &root_, access, 0, 0) != ERROR_SUCCESS)
        {
            root_ = nullptr;
        }
    }

    ~MountedHive()
    {
        if (root_ != nullptr)
        {
            RegCloseKey(root_);
        }
    }

    MountedHive(const MountedHive&) = delete;
    MountedHive& operator=(const MountedHive&) = delete;

    /**
     * @brief The root key handle of the mount.
     * @return The handle, null when the mount failed.
     */
    HKEY get() const
    {
        return root_;
    }

    /**
     * @brief Whether the hive was mounted.
     * @return true when the root handle is usable.
     */
    bool IsOpen() const
    {
        return root_ != nullptr;
    }

private:
    HKEY root_ = nullptr; /* Root key handle of the mount. */
};

/**
 * @brief Create a key below a mounted hive, creating its parents.
 * @param[in] root Root key handle of the mount.
 * @param[in] path Path of the key from the hive root.
 * @return The handle of the key, null on failure.
 */
HKEY EnsureKey(HKEY root, const std::wstring& path)
{
    HKEY  key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &key,
                        &disposition) != ERROR_SUCCESS)
    {
        return nullptr;
    }

    return key;
}

/**
 * @brief Write a value into a key of a mounted hive.
 * @param[in] root Root key handle of the mount.
 * @param[in] path Path of the key which receives the value.
 * @param[in] name Name of the value, empty for the default value.
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data Raw data of the value.
 * @return true on success.
 */
bool SetValue(HKEY root, const std::wstring& path, const std::wstring& name, DWORD type, const std::vector<BYTE>& data)
{
    HKEY key = EnsureKey(root, path);
    if (key == nullptr)
    {
        return false;
    }

    static BYTE empty = 0;
    const BYTE* payload = data.empty() ? &empty : data.data();
    const LONG  status = RegSetValueExW(key, name.c_str(), 0, type, payload, static_cast<DWORD>(data.size()));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

/**
 * @brief Read the type and the raw data of a value of a mounted hive.
 * @param[in] root Root key handle of the mount.
 * @param[in] path Path of the key which holds the value.
 * @param[in] name Name of the value, empty for the default value.
 * @param[out] type The `REG_*` type code of the value.
 * @param[out] data Raw data of the value.
 * @return true on success.
 */
bool ReadValue(HKEY root, const std::wstring& path, const std::wstring& name, DWORD& type, std::vector<BYTE>& data)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD size = 0;
    LONG  status = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA)
    {
        RegCloseKey(key);
        return false;
    }

    data.resize(size);
    DWORD read = size;
    status = RegQueryValueExW(key, name.c_str(), nullptr, &type, data.empty() ? nullptr : data.data(), &read);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
    {
        return false;
    }

    data.resize(read);
    return true;
}

/**
 * @brief Whether a mounted hive holds a key.
 * @param[in] root Root key handle of the mount.
 * @param[in] path Path of the key from the hive root.
 * @return true when the key exists.
 */
bool KeyExists(HKEY root, const std::wstring& path)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    RegCloseKey(key);
    return true;
}

/**
 * @brief Build the raw data of a `REG_SZ` value.
 * @param[in] text Text of the value.
 * @return The UTF-16 data with its trailing terminator.
 */
std::vector<BYTE> StringData(const std::wstring& text)
{
    std::vector<BYTE> data((text.size() + 1) * sizeof(wchar_t), 0);
    memcpy(data.data(), text.c_str(), text.size() * sizeof(wchar_t));
    return data;
}

/**
 * @brief Build the raw data of a `REG_MULTI_SZ` value.
 * @param[in] items Items of the value.
 * @return The UTF-16 items with their terminators.
 */
std::vector<BYTE> MultiStringData(const std::vector<std::wstring>& items)
{
    std::vector<BYTE> data;
    for (const auto& item : items)
    {
        const auto offset = data.size();
        data.resize(offset + (item.size() + 1) * sizeof(wchar_t), 0);
        memcpy(data.data() + offset, item.c_str(), item.size() * sizeof(wchar_t));
    }

    data.resize(data.size() + sizeof(wchar_t), 0);
    return data;
}

/**
 * @brief Read the text of a `REG_SZ` value.
 * @param[in] data Raw data of the value.
 * @return The text without its terminator.
 */
std::wstring StringOf(const std::vector<BYTE>& data)
{
    if (data.size() < sizeof(wchar_t))
    {
        return {};
    }

    return std::wstring(reinterpret_cast<const wchar_t*>(data.data()), (data.size() / sizeof(wchar_t)) - 1);
}

/** Path of the hive the merge writes into. */
constexpr wchar_t kTargetHive[] = L"appbox_unit_hive_merge_target.hiv";

/** Path of the hive the merge applies on top of the target. */
constexpr wchar_t kSourceHive[] = L"appbox_unit_hive_merge_source.hiv";

/** Key of the test inside the virtual registry. */
constexpr wchar_t kKeyPath[] = L"HKEY_CURRENT_USER\\Software\\AppBoxMerge";

} // namespace

/**
 * @brief The content of the source is applied to a target which is created.
 *
 * A run whose state directory does not carry a hive yet is the normal case of
 * a sandbox which was never started: mounting the hive creates the file, and
 * the merge has to fill it with the content of the package.
 */
TEST(Unit_HiveMerge, AppliesTheContentOfTheSourceHive)
{
    const auto target_path = HivePath(kTargetHive);
    const auto source_path = HivePath(kSourceHive);
    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);

    {
        MountedHive source(source_path);
        ASSERT_TRUE(source.IsOpen());
        ASSERT_TRUE(SetValue(source.get(), kKeyPath, L"Value", REG_SZ, StringData(L"package")));
    }

    std::string error;
    ASSERT_TRUE(appbox::MergeHiveInto(target_path, source_path, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(target_path));

    MountedHive target(target_path, KEY_READ);
    ASSERT_TRUE(target.IsOpen());

    DWORD             type = REG_NONE;
    std::vector<BYTE> data;
    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"Value", type, data));
    EXPECT_EQ(type, REG_SZ);
    EXPECT_EQ(StringOf(data), L"package");

    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);
}

/**
 * @brief The entries the source names override the entries of the target.
 *
 * The merge is the loader side of a patch layer: the entries of the package
 * win over the entries of the same name of the layers below it, while an entry
 * no package names keeps the content below it.
 */
TEST(Unit_HiveMerge, OverridesTheEntriesItNames)
{
    const auto target_path = HivePath(kTargetHive);
    const auto source_path = HivePath(kSourceHive);
    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);

    {
        MountedHive target(target_path);
        ASSERT_TRUE(target.IsOpen());
        ASSERT_TRUE(SetValue(target.get(), kKeyPath, L"Value", REG_SZ, StringData(L"archive")));
        ASSERT_TRUE(SetValue(target.get(), kKeyPath, L"Kept", REG_SZ, StringData(L"keep")));
    }

    {
        MountedHive source(source_path);
        ASSERT_TRUE(source.IsOpen());
        ASSERT_TRUE(SetValue(source.get(), kKeyPath, L"Value", REG_SZ, StringData(L"package")));
        ASSERT_TRUE(SetValue(source.get(), kKeyPath, L"Added", REG_SZ, StringData(L"added")));
    }

    std::string error;
    ASSERT_TRUE(appbox::MergeHiveInto(target_path, source_path, error)) << error;

    MountedHive target(target_path, KEY_READ);
    ASSERT_TRUE(target.IsOpen());

    DWORD             type = REG_NONE;
    std::vector<BYTE> data;

    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"Value", type, data));
    EXPECT_EQ(StringOf(data), L"package");

    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"Added", type, data));
    EXPECT_EQ(StringOf(data), L"added");

    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"Kept", type, data));
    EXPECT_EQ(StringOf(data), L"keep");

    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);
}

/**
 * @brief Every value is copied with its type, the default value included.
 *
 * The default value of a key is enumerated with an empty name and is a value
 * like every other one, so a package which sets it has to override the default
 * value of the layers below it.
 */
TEST(Unit_HiveMerge, CopiesTheTypesAndTheDefaultValue)
{
    const auto target_path = HivePath(kTargetHive);
    const auto source_path = HivePath(kSourceHive);
    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);

    const std::vector<BYTE> dword_data = { 0x78, 0x56, 0x34, 0x12 };
    const std::vector<BYTE> multi_data = MultiStringData({ L"one", L"two" });
    const std::vector<BYTE> binary_data = { 0x00, 0x01, 0xfe, 0xff };

    {
        MountedHive source(source_path);
        ASSERT_TRUE(source.IsOpen());
        ASSERT_TRUE(SetValue(source.get(), kKeyPath, L"", REG_SZ, StringData(L"default")));
        ASSERT_TRUE(SetValue(source.get(), kKeyPath, L"Dword", REG_DWORD, dword_data));
        ASSERT_TRUE(SetValue(source.get(), kKeyPath, L"Multi", REG_MULTI_SZ, multi_data));
        ASSERT_TRUE(SetValue(source.get(), kKeyPath, L"Binary", REG_BINARY, binary_data));
    }

    std::string error;
    ASSERT_TRUE(appbox::MergeHiveInto(target_path, source_path, error)) << error;

    MountedHive target(target_path, KEY_READ);
    ASSERT_TRUE(target.IsOpen());

    DWORD             type = REG_NONE;
    std::vector<BYTE> data;

    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"", type, data));
    EXPECT_EQ(type, REG_SZ);
    EXPECT_EQ(StringOf(data), L"default");

    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"Dword", type, data));
    EXPECT_EQ(type, REG_DWORD);
    EXPECT_EQ(data, dword_data);

    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"Multi", type, data));
    EXPECT_EQ(type, REG_MULTI_SZ);
    EXPECT_EQ(data, multi_data);

    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"Binary", type, data));
    EXPECT_EQ(type, REG_BINARY);
    EXPECT_EQ(data, binary_data);

    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);
}

/**
 * @brief The merge descends into the sub keys of both hives.
 *
 * A key which both hives hold is merged and a key only one of them holds stays
 * in place, so a package can add a branch to the tree of the layers below it.
 */
TEST(Unit_HiveMerge, MergesNestedKeys)
{
    const auto target_path = HivePath(kTargetHive);
    const auto source_path = HivePath(kSourceHive);
    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);

    {
        MountedHive target(target_path);
        ASSERT_TRUE(target.IsOpen());
        ASSERT_TRUE(SetValue(target.get(), kKeyPath, L"Value", REG_SZ, StringData(L"archive")));
        ASSERT_TRUE(SetValue(target.get(), std::wstring(kKeyPath) + L"\\Below", L"Kept", REG_SZ, StringData(L"keep")));
    }

    {
        MountedHive source(source_path);
        ASSERT_TRUE(source.IsOpen());
        ASSERT_TRUE(SetValue(source.get(), std::wstring(kKeyPath) + L"\\Below\\Deeper", L"Deep", REG_SZ,
                             StringData(L"package")));
        ASSERT_TRUE(
            SetValue(source.get(), std::wstring(kKeyPath) + L"\\Below", L"Value", REG_SZ, StringData(L"package")));
    }

    std::string error;
    ASSERT_TRUE(appbox::MergeHiveInto(target_path, source_path, error)) << error;

    MountedHive target(target_path, KEY_READ);
    ASSERT_TRUE(target.IsOpen());

    DWORD             type = REG_NONE;
    std::vector<BYTE> data;

    ASSERT_TRUE(ReadValue(target.get(), std::wstring(kKeyPath) + L"\\Below\\Deeper", L"Deep", type, data));
    EXPECT_EQ(StringOf(data), L"package");

    ASSERT_TRUE(ReadValue(target.get(), std::wstring(kKeyPath) + L"\\Below", L"Value", type, data));
    EXPECT_EQ(StringOf(data), L"package");

    ASSERT_TRUE(ReadValue(target.get(), std::wstring(kKeyPath) + L"\\Below", L"Kept", type, data));
    EXPECT_EQ(StringOf(data), L"keep");

    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"Value", type, data));
    EXPECT_EQ(StringOf(data), L"archive");

    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);
}

/**
 * @brief A package whose hive is empty keeps the hive below it.
 *
 * A patch package which carries no registry content is the normal case of a
 * package which only updates the filesystem: the merge applies nothing and the
 * hive of the layers below it stays in place.
 */
TEST(Unit_HiveMerge, AnEmptySourceHiveKeepsTheTarget)
{
    const auto target_path = HivePath(kTargetHive);
    const auto source_path = HivePath(kSourceHive);
    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);

    {
        MountedHive target(target_path);
        ASSERT_TRUE(target.IsOpen());
        ASSERT_TRUE(SetValue(target.get(), kKeyPath, L"Value", REG_SZ, StringData(L"archive")));
    }

    /* An empty hive: the mount creates the file, the merge finds no entry. */
    {
        MountedHive source(source_path);
        ASSERT_TRUE(source.IsOpen());
    }

    std::string error;
    ASSERT_TRUE(appbox::MergeHiveInto(target_path, source_path, error)) << error;

    MountedHive target(target_path, KEY_READ);
    ASSERT_TRUE(target.IsOpen());

    DWORD             type = REG_NONE;
    std::vector<BYTE> data;
    ASSERT_TRUE(ReadValue(target.get(), kKeyPath, L"Value", type, data));
    EXPECT_EQ(StringOf(data), L"archive");

    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);
}

/**
 * @brief A source which does not exist is refused.
 *
 * The source is a resource of a package: mounting it would create an empty
 * hive inside the cache entry of the package, which would look like the
 * content of the package, so a missing source fails the merge instead. The
 * target is not touched, because the source is checked first.
 */
TEST(Unit_HiveMerge, AMissingSourceIsRefused)
{
    const auto target_path = HivePath(kTargetHive);
    const auto source_path = HivePath(kSourceHive);
    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);

    std::string error;
    EXPECT_FALSE(appbox::MergeHiveInto(target_path, source_path, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(std::filesystem::exists(source_path));
    EXPECT_FALSE(std::filesystem::exists(target_path));

    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);
}

/**
 * @brief The whiteout store of the target survives the merge.
 *
 * The store records the deletions of the sandbox and is not part of the
 * content of a layer, so a package which carries a key of that name neither
 * overrides the store of the target nor drops it.
 */
TEST(Unit_HiveMerge, KeepsTheWhiteoutStoreOfTheTarget)
{
    const auto target_path = HivePath(kTargetHive);
    const auto source_path = HivePath(kSourceHive);
    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);

    const std::wstring store_path = appbox::registry_whiteout::kStoreKey;
    const std::wstring marker_path = store_path + L"\\K\\" + kKeyPath;

    {
        MountedHive target(target_path);
        ASSERT_TRUE(target.IsOpen());

        HKEY marker = EnsureKey(target.get(), marker_path);
        ASSERT_NE(marker, nullptr);
        RegCloseKey(marker);
    }

    {
        MountedHive source(source_path);
        ASSERT_TRUE(source.IsOpen());
        ASSERT_TRUE(SetValue(source.get(), store_path + L"\\K\\HKEY_CURRENT_USER\\Software\\Other", L"marker",
                             REG_DWORD, { 1, 0, 0, 0 }));
    }

    std::string error;
    ASSERT_TRUE(appbox::MergeHiveInto(target_path, source_path, error)) << error;

    MountedHive target(target_path, KEY_READ);
    ASSERT_TRUE(target.IsOpen());

    EXPECT_TRUE(KeyExists(target.get(), marker_path));
    EXPECT_FALSE(KeyExists(target.get(), store_path + L"\\K\\HKEY_CURRENT_USER\\Software\\Other"));

    RemoveHiveFiles(target_path);
    RemoveHiveFiles(source_path);
}
