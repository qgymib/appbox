#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include "RegistryIsolation.hpp"
#include "registry/HiveReader.hpp"

namespace
{

/**
 * @brief Path of the hive file used by the tests of this file.
 *
 * The unit tests run sequentially, so one shared path below the temp directory
 * with a per test cleanup is enough.
 * @return The DOS path of the hive file.
 */
std::wstring TestHivePath()
{
    return (std::filesystem::temp_directory_path() / L"appbox_unit_hive_reader.hiv").wstring();
}

/**
 * @brief Remove the test hive file, ignoring errors.
 */
void RemoveTestHive()
{
    std::error_code ec;
    std::filesystem::remove(TestHivePath(), ec);
}

/**
 * @brief RAII writer of the test hive.
 *
 * The writer mounts the hive file with RegLoadAppKeyW in write mode and
 * releases the mount on destruction, which flushes the file like the sandboxed
 * process does when it exits.
 */
class HiveWriter
{
public:
    /**
     * @brief Mount the test hive in write mode, creating the file.
     */
    HiveWriter()
    {
        RemoveTestHive();
        const LONG ret = RegLoadAppKeyW(TestHivePath().c_str(), &root_, KEY_ALL_ACCESS, 0, 0);
        if (ret != ERROR_SUCCESS)
        {
            root_ = nullptr;
        }
    }

    ~HiveWriter()
    {
        if (root_ != nullptr)
        {
            RegCloseKey(root_);
        }
    }

    HiveWriter(const HiveWriter&) = delete;
    HiveWriter& operator=(const HiveWriter&) = delete;

    /**
     * @brief Whether the hive was mounted.
     * @return true when the writer holds the root handle.
     */
    bool IsValid() const
    {
        return root_ != nullptr;
    }

    /**
     * @brief Create (or open) a key below the hive root.
     * @param[in] relative The key path relative to the root, intermediate keys
     *                     are created automatically.
     * @return true on success.
     */
    bool CreateKey(const std::wstring& relative)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(root_, relative.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
            ERROR_SUCCESS)
        {
            return false;
        }
        RegCloseKey(key);
        return true;
    }

    /**
     * @brief Write a value below the hive root.
     * @param[in] relative The key path relative to the root, created when missing.
     * @param[in] name The value name, null for the default value.
     * @param[in] type The value type.
     * @param[in] data The value bytes.
     * @param[in] size The number of bytes.
     * @return true on success.
     */
    bool WriteValue(const std::wstring& relative, const wchar_t* name, DWORD type, const void* data, DWORD size)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(root_, relative.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
            ERROR_SUCCESS)
        {
            return false;
        }

        const LONG ret = RegSetValueExW(key, name, 0, type, static_cast<const BYTE*>(data), size);
        RegCloseKey(key);
        return ret == ERROR_SUCCESS;
    }

private:
    HKEY root_ = nullptr;
};

/**
 * @brief Find a value by name.
 * @param[in] values The enumerated values.
 * @param[in] name The value name.
 * @return The value or null when the name is not present.
 */
const appbox::RegistryValue* FindValue(const std::vector<appbox::RegistryValue>& values, const std::wstring& name)
{
    for (const auto& value : values)
    {
        if (value.name == name)
        {
            return &value;
        }
    }
    return nullptr;
}

} // namespace

/**
 * @brief A missing hive file is reported as missing and is not created.
 */
TEST(UnitHiveReader, OpenMissingFile)
{
    RemoveTestHive();

    appbox::HiveReader reader;
    ASSERT_FALSE(reader.Open(TestHivePath()));
    ASSERT_FALSE(reader.IsOpen());
    ASSERT_TRUE(reader.IsMissing());

    /* The loader must not create the file of the sandbox owned hive. */
    ASSERT_FALSE(std::filesystem::exists(TestHivePath()));
}

/**
 * @brief The sub keys of the root and of nested keys are enumerated.
 */
TEST(UnitHiveReader, EnumSubKeys)
{
    {
        HiveWriter writer;
        ASSERT_TRUE(writer.IsValid());
        ASSERT_TRUE(writer.CreateKey(L"Software"));
        ASSERT_TRUE(writer.CreateKey(L"Software\\Vendor"));
        ASSERT_TRUE(writer.CreateKey(L"Software\\Vendor\\App"));
        ASSERT_TRUE(writer.CreateKey(L"Zeta"));
    }

    appbox::HiveReader reader;
    ASSERT_TRUE(reader.Open(TestHivePath()));
    ASSERT_TRUE(reader.IsOpen());
    ASSERT_FALSE(reader.IsMissing());

    std::vector<std::wstring> names;
    ASSERT_TRUE(reader.EnumSubKeys(L"", names));
    ASSERT_EQ(names.size(), 2u);
    ASSERT_NE(std::find(names.begin(), names.end(), L"Software"), names.end());
    ASSERT_NE(std::find(names.begin(), names.end(), L"Zeta"), names.end());

    ASSERT_TRUE(reader.EnumSubKeys(L"Software", names));
    ASSERT_EQ(names.size(), 1u);
    ASSERT_EQ(names[0], L"Vendor");

    ASSERT_TRUE(reader.EnumSubKeys(L"Software\\Vendor", names));
    ASSERT_EQ(names.size(), 1u);
    ASSERT_EQ(names[0], L"App");

    /* A key without sub keys yields an empty list, a missing key fails. */
    ASSERT_TRUE(reader.EnumSubKeys(L"Software\\Vendor\\App", names));
    ASSERT_TRUE(names.empty());
    ASSERT_FALSE(reader.EnumSubKeys(L"DoesNotExist", names));

    /* Surrounding separators are tolerated. */
    ASSERT_TRUE(reader.EnumSubKeys(L"\\Software\\", names));
    ASSERT_EQ(names.size(), 1u);

    reader.Close();
    RemoveTestHive();
}

/**
 * @brief The values of a key are enumerated with name, type and raw data.
 */
TEST(UnitHiveReader, EnumValues)
{
    const wchar_t  text[]    = L"Hello";
    const DWORD    dword     = 16;
    const BYTE     bytes[3]  = {0x48, 0x65, 0x6c};
    const wchar_t  multi[]   = L"first\0second\0"; /* Trailing NUL of the literal terminates the list. */

    {
        HiveWriter writer;
        ASSERT_TRUE(writer.IsValid());
        ASSERT_TRUE(writer.WriteValue(L"", nullptr, REG_SZ, text, sizeof(text)));
        ASSERT_TRUE(writer.WriteValue(L"", L"Text", REG_SZ, text, sizeof(text)));
        ASSERT_TRUE(writer.WriteValue(L"", L"Number", REG_DWORD, &dword, sizeof(dword)));
        ASSERT_TRUE(writer.WriteValue(L"", L"Bytes", REG_BINARY, bytes, sizeof(bytes)));
        ASSERT_TRUE(writer.WriteValue(L"", L"Multi", REG_MULTI_SZ, multi, sizeof(multi)));
    }

    appbox::HiveReader reader;
    ASSERT_TRUE(reader.Open(TestHivePath()));

    std::vector<appbox::RegistryValue> values;
    ASSERT_TRUE(reader.EnumValues(L"", values));
    ASSERT_EQ(values.size(), 5u);

    /* The default value has an empty name. */
    const auto* def = FindValue(values, L"");
    ASSERT_NE(def, nullptr);
    ASSERT_EQ(def->type, REG_SZ);
    ASSERT_EQ(appbox::ValueDataAsString(*def), L"Hello");

    const auto* text_value = FindValue(values, L"Text");
    ASSERT_NE(text_value, nullptr);
    ASSERT_EQ(text_value->type, REG_SZ);
    ASSERT_EQ(appbox::ValueDataAsString(*text_value), L"Hello");

    const auto* number = FindValue(values, L"Number");
    ASSERT_NE(number, nullptr);
    ASSERT_EQ(number->type, REG_DWORD);
    ASSERT_EQ(number->data.size(), sizeof(DWORD));

    const auto* binary = FindValue(values, L"Bytes");
    ASSERT_NE(binary, nullptr);
    ASSERT_EQ(binary->type, REG_BINARY);
    ASSERT_EQ(binary->data.size(), 3u);
    ASSERT_EQ(binary->data[0], 0x48);
    ASSERT_EQ(binary->data[2], 0x6c);

    const auto* multi_value = FindValue(values, L"Multi");
    ASSERT_NE(multi_value, nullptr);
    ASSERT_EQ(multi_value->type, REG_MULTI_SZ);

    /* A key without values yields an empty list, a missing key fails. */
    {
        HiveWriter writer;
        ASSERT_TRUE(writer.IsValid());
        ASSERT_TRUE(writer.CreateKey(L"Empty"));
    }
    ASSERT_TRUE(reader.Refresh());
    ASSERT_TRUE(reader.EnumValues(L"Empty", values));
    ASSERT_TRUE(values.empty());
    ASSERT_FALSE(reader.EnumValues(L"DoesNotExist", values));

    reader.Close();
    RemoveTestHive();
}

/**
 * @brief HasSubKeys reflects the sub key count of a key.
 */
TEST(UnitHiveReader, HasSubKeys)
{
    {
        HiveWriter writer;
        ASSERT_TRUE(writer.IsValid());
        ASSERT_TRUE(writer.CreateKey(L"Software"));
        ASSERT_TRUE(writer.CreateKey(L"Software\\Leaf"));
    }

    appbox::HiveReader reader;
    ASSERT_TRUE(reader.Open(TestHivePath()));

    ASSERT_TRUE(reader.HasSubKeys(L""));
    ASSERT_TRUE(reader.HasSubKeys(L"Software"));
    ASSERT_FALSE(reader.HasSubKeys(L"Software\\Leaf"));
    ASSERT_FALSE(reader.HasSubKeys(L"DoesNotExist"));

    reader.Close();
    RemoveTestHive();
}

/**
 * @brief The whiteout store of the sandbox is not part of the view.
 *
 * The store is a reserved key at the root of the hive which carries the
 * markers of the entries the sandbox deleted. The browser shows the five root
 * keys of the view, so the root enumeration and the root sub key check skip
 * it, while the key itself stays reachable for the sandbox.
 */
TEST(UnitHiveReader, RootHidesWhiteoutStore)
{
    const std::wstring store = appbox::registry_whiteout::kStoreKey;

    {
        HiveWriter writer;
        ASSERT_TRUE(writer.IsValid());
        ASSERT_TRUE(writer.CreateKey(L"HKEY_CURRENT_USER"));
        ASSERT_TRUE(writer.CreateKey(store));
        ASSERT_TRUE(writer.CreateKey(store + L"\\K\\HKEY_CURRENT_USER"));
    }

    appbox::HiveReader reader;
    ASSERT_TRUE(reader.Open(TestHivePath()));

    std::vector<std::wstring> names;
    ASSERT_TRUE(reader.EnumSubKeys(L"", names));
    ASSERT_EQ(names.size(), 1u);
    ASSERT_EQ(names[0], L"HKEY_CURRENT_USER");

    /* Only the view hides the store, the hive still holds it. */
    ASSERT_TRUE(reader.HasSubKeys(store));

    reader.Close();
    RemoveTestHive();

    /* A hive which only holds the store has no visible sub key at its root. */
    {
        HiveWriter writer;
        ASSERT_TRUE(writer.IsValid());
        ASSERT_TRUE(writer.CreateKey(store));
    }

    ASSERT_TRUE(reader.Open(TestHivePath()));
    ASSERT_FALSE(reader.HasSubKeys(L""));
    ASSERT_TRUE(reader.EnumSubKeys(L"", names));
    ASSERT_TRUE(names.empty());

    reader.Close();
    RemoveTestHive();
}

/**
 * @brief Refresh remounts the file, so flushed changes of a second writer are
 *        guaranteed to be visible after the call.
 *
 * The kernel may share the hive image between mounts of the same file, so the
 * changes can already be visible before the refresh; the refresh is the
 * guaranteed way to pick them up.
 */
TEST(UnitHiveReader, RefreshPicksUpFlushedChanges)
{
    {
        HiveWriter writer;
        ASSERT_TRUE(writer.IsValid());
        ASSERT_TRUE(writer.CreateKey(L"First"));
    }

    appbox::HiveReader reader;
    ASSERT_TRUE(reader.Open(TestHivePath()));

    std::vector<std::wstring> names;
    ASSERT_TRUE(reader.EnumSubKeys(L"", names));
    ASSERT_EQ(names.size(), 1u);
    ASSERT_EQ(names[0], L"First");

    {
        /* A second writer flushes an additional key into the file. */
        HiveWriter writer;
        ASSERT_TRUE(writer.IsValid());
        ASSERT_TRUE(writer.CreateKey(L"Second"));
    }

    /*
     * Changes may already be visible without a refresh when the kernel shares
     * the hive image between mounts, but the refresh guarantees visibility of
     * the flushed state in every case.
     */
    ASSERT_TRUE(reader.EnumSubKeys(L"", names));
    ASSERT_EQ(names.size(), 2u);
    ASSERT_NE(std::find(names.begin(), names.end(), L"First"), names.end());
    ASSERT_NE(std::find(names.begin(), names.end(), L"Second"), names.end());

    /* The refresh remounts the file and keeps showing the flushed key. */
    ASSERT_TRUE(reader.Refresh());
    ASSERT_TRUE(reader.EnumSubKeys(L"", names));
    ASSERT_EQ(names.size(), 2u);
    ASSERT_NE(std::find(names.begin(), names.end(), L"Second"), names.end());

    reader.Close();
    RemoveTestHive();
}

/**
 * @brief Value types are formatted with their registry editor names.
 */
TEST(UnitHiveReader, FormatValueTypeName)
{
    ASSERT_EQ(appbox::FormatValueTypeName(REG_SZ), L"REG_SZ");
    ASSERT_EQ(appbox::FormatValueTypeName(REG_EXPAND_SZ), L"REG_EXPAND_SZ");
    ASSERT_EQ(appbox::FormatValueTypeName(REG_BINARY), L"REG_BINARY");
    ASSERT_EQ(appbox::FormatValueTypeName(REG_DWORD), L"REG_DWORD");
    ASSERT_EQ(appbox::FormatValueTypeName(REG_MULTI_SZ), L"REG_MULTI_SZ");
    ASSERT_EQ(appbox::FormatValueTypeName(REG_QWORD), L"REG_QWORD");
    ASSERT_EQ(appbox::FormatValueTypeName(0x7f), L"REG_0x7f");
}

/**
 * @brief Value data is formatted like the registry editor list column.
 */
TEST(UnitHiveReader, FormatValueData)
{
    appbox::RegistryValue value;

    /* Strings are shown verbatim, empty ones as not set. */
    value.type = REG_SZ;
    value.data = std::vector<BYTE>{0x48, 0x00, 0x69, 0x00, 0x00, 0x00}; /* L"Hi\0" */
    ASSERT_EQ(appbox::FormatValueData(value, 0), L"Hi");
    value.data.clear();
    ASSERT_EQ(appbox::FormatValueData(value, 0), L"(value not set)");

    /* DWORD is shown as hexadecimal and decimal. */
    value.type = REG_DWORD;
    value.data = std::vector<BYTE>{0x10, 0x00, 0x00, 0x00};
    ASSERT_EQ(appbox::FormatValueData(value, 0), L"0x00000010 (16)");

    /* QWORD is shown as hexadecimal and decimal. */
    value.type = REG_QWORD;
    value.data = std::vector<BYTE>{0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ASSERT_EQ(appbox::FormatValueData(value, 0), L"0x0000000000000010 (16)");

    /* MULTI_SZ entries are joined with a space. */
    value.type = REG_MULTI_SZ;
    value.data = std::vector<BYTE>{L'a', 0x00, 0x00, 0x00, L'b', 0x00, 0x00, 0x00, 0x00, 0x00}; /* L"a\0b\0\0" */
    ASSERT_EQ(appbox::FormatValueData(value, 0), L"a b");

    /* Binary data becomes a byte hex dump, empty data a placeholder. */
    value.type = REG_BINARY;
    value.data = std::vector<BYTE>{0x48, 0x65, 0x6c};
    ASSERT_EQ(appbox::FormatValueData(value, 0), L"48 65 6c");
    value.data.clear();
    ASSERT_EQ(appbox::FormatValueData(value, 0), L"(zero-length binary value)");

    /* Long output is truncated with an ellipsis. */
    value.data = std::vector<BYTE>(32, 0xab);
    ASSERT_EQ(appbox::FormatValueData(value, 8), L"ab ab...");
}

/**
 * @brief The hex dump groups the bytes into offset addressed lines.
 */
TEST(UnitHiveReader, FormatHexDump)
{
    ASSERT_EQ(appbox::FormatHexDump({}), L"");

    const std::vector<BYTE> data = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    ASSERT_EQ(appbox::FormatHexDump(data),
              L"00000000  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\r\n"
              L"00000010  10");
}
