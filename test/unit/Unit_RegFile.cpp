#include <gtest/gtest.h>
#include "src/core/RegFile.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / (L"appbox-regfile-" + UniqueFragment());
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    /**
     * @brief Build a path inside the temporary folder.
     * @param[in] name File name.
     * @return The path of the file.
     */
    std::filesystem::path File(const std::wstring& name) const
    {
        return path_ / name;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Write raw bytes into a file.
 * @param[in] path Path of the file.
 * @param[in] bytes Bytes to write.
 */
void WriteBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/**
 * @brief Encode a text as UTF-16 little endian with a byte order mark.
 * @param[in] text Text to encode.
 * @return The encoded bytes.
 */
std::vector<unsigned char> Utf16Le(const std::wstring& text)
{
    std::vector<unsigned char> bytes = { 0xFF, 0xFE };
    for (const wchar_t character : text)
    {
        const auto code = static_cast<unsigned int>(character);
        bytes.push_back(static_cast<unsigned char>(code & 0xFF));
        bytes.push_back(static_cast<unsigned char>((code >> 8) & 0xFF));
    }
    return bytes;
}

/**
 * @brief Find the entry of a value inside a parsed file.
 * @param[in] entries Entries to search.
 * @param[in] name Name of the value.
 * @return The entry, null when it does not exist.
 */
const appbox::RegFileEntry* FindEntry(const std::vector<appbox::RegFileEntry>& entries,
                                      const std::wstring& name)
{
    for (const auto& entry : entries)
    {
        if (entry.operation == appbox::RegFileOperation::SetValue && entry.value_name == name)
        {
            return &entry;
        }
    }
    return nullptr;
}

/** Text of a registry file which uses every supported data form. */
const wchar_t* const kSampleText =
    L"Windows Registry Editor Version 5.00\r\n"
    L"\r\n"
    L"; a comment\r\n"
    L"[HKEY_CURRENT_USER\\Software\\Vendor]\r\n"
    L"@=\"default\"\r\n"
    L"\"Name\"=\"App\"\r\n"
    L"\"Count\"=dword:0000001e\r\n"
    L"\"Small\"=dword:1e\r\n"
    L"\"Multi\"=hex(7):6f,00,6e,00,65,00,00,00,74,00,77,00,6f,00,00,00,00,00\r\n"
    L"\"Wide\"=hex(b):01,00,00,00,00,00,00,00\r\n"
    L"\"Expand\"=hex(2):25,00,50,00,41,00,54,00,48,00,25,00,00,00\r\n"
    L"\"Text\"=hex(1):61,00,00,00\r\n"
    L"\"Raw\"=hex:01,02\r\n"
    L"\"Empty\"=hex(0):\r\n"
    L"\"Blob\"=hex:01,02,\\\r\n"
    L"  03,04\r\n"
    L"\"Gone\"=-\r\n"
    L"[HKEY_CURRENT_USER\\Software\\Temp]\r\n"
    L"[-HKEY_CURRENT_USER\\Software\\Temp]\r\n"
    L"[HKLM\\Software\\Legacy]\r\n"
    L"\"Path\"=\"C:\\\\App\\\\\"\r\n";

} // namespace

TEST(UnitRegFile, ParsesEverySupportedDataForm)
{
    std::vector<appbox::RegFileEntry> entries;
    std::string error;

    ASSERT_TRUE(appbox::ParseRegText(kSampleText, entries, error)) << error;
    ASSERT_EQ(entries.size(), 17u);

    EXPECT_EQ(entries[0].operation, appbox::RegFileOperation::AddKey);
    EXPECT_EQ(entries[0].key_path, L"HKEY_CURRENT_USER\\Software\\Vendor");

    const auto* default_value = FindEntry(entries, L"");
    ASSERT_NE(default_value, nullptr);
    EXPECT_EQ(default_value->type, appbox::RegistryValueType::String);
    EXPECT_EQ(appbox::RegistryStringValue(default_value->data), L"default");

    const auto* name = FindEntry(entries, L"Name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(appbox::RegistryStringValue(name->data), L"App");

    const auto* count = FindEntry(entries, L"Count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->type, appbox::RegistryValueType::Dword);
    std::uint32_t number = 0;
    ASSERT_TRUE(appbox::RegistryDwordValue(count->data, number));
    EXPECT_EQ(number, 30u);

    const auto* small = FindEntry(entries, L"Small");
    ASSERT_NE(small, nullptr);
    ASSERT_TRUE(appbox::RegistryDwordValue(small->data, number));
    EXPECT_EQ(number, 30u);

    const auto* multi = FindEntry(entries, L"Multi");
    ASSERT_NE(multi, nullptr);
    EXPECT_EQ(multi->type, appbox::RegistryValueType::MultiString);
    EXPECT_EQ(appbox::RegistryMultiStringValue(multi->data), (std::vector<std::wstring>{ L"one", L"two" }));

    const auto* wide = FindEntry(entries, L"Wide");
    ASSERT_NE(wide, nullptr);
    EXPECT_EQ(wide->type, appbox::RegistryValueType::Qword);
    std::uint64_t qword = 0;
    ASSERT_TRUE(appbox::RegistryQwordValue(wide->data, qword));
    EXPECT_EQ(qword, 1u);

    const auto* expand = FindEntry(entries, L"Expand");
    ASSERT_NE(expand, nullptr);
    EXPECT_EQ(expand->type, appbox::RegistryValueType::ExpandString);
    EXPECT_EQ(appbox::RegistryStringValue(expand->data), L"%PATH%");

    const auto* text = FindEntry(entries, L"Text");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->type, appbox::RegistryValueType::String);
    EXPECT_EQ(appbox::RegistryStringValue(text->data), L"a");

    const auto* raw = FindEntry(entries, L"Raw");
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->type, appbox::RegistryValueType::Binary);
    EXPECT_EQ(raw->data, (std::vector<std::uint8_t>{ 0x01, 0x02 }));

    const auto* empty = FindEntry(entries, L"Empty");
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(empty->type, appbox::RegistryValueType::None);
    EXPECT_TRUE(empty->data.empty());

    /* The hexadecimal block is split over two lines. */
    const auto* blob = FindEntry(entries, L"Blob");
    ASSERT_NE(blob, nullptr);
    EXPECT_EQ(blob->data, (std::vector<std::uint8_t>{ 0x01, 0x02, 0x03, 0x04 }));

    const auto* gone = FindEntry(entries, L"Gone");
    EXPECT_EQ(gone, nullptr);
    bool has_removal = false;
    for (const auto& entry : entries)
    {
        if (entry.operation == appbox::RegFileOperation::RemoveValue && entry.value_name == L"Gone")
        {
            has_removal = true;
        }
    }
    EXPECT_TRUE(has_removal);

    EXPECT_EQ(entries[13].operation, appbox::RegFileOperation::AddKey);
    EXPECT_EQ(entries[13].key_path, L"HKEY_CURRENT_USER\\Software\\Temp");
    EXPECT_EQ(entries[14].operation, appbox::RegFileOperation::RemoveKey);
    EXPECT_EQ(entries[14].key_path, L"HKEY_CURRENT_USER\\Software\\Temp");

    /* The abbreviated root name is expanded. */
    EXPECT_EQ(entries[15].key_path, L"HKEY_LOCAL_MACHINE\\Software\\Legacy");
    EXPECT_EQ(entries[16].operation, appbox::RegFileOperation::SetValue);
}

TEST(UnitRegFile, ParsesEscapedStrings)
{
    std::vector<appbox::RegFileEntry> entries;
    std::string error;

    const std::wstring text = L"REGEDIT4\r\n"
                              L"[HKEY_CURRENT_USER\\Software\\Vendor]\r\n"
                              L"\"Path\"=\"C:\\\\App\\\\\"\r\n"
                              L"\"Quote\"=\"a\\\"b\"\r\n";

    ASSERT_TRUE(appbox::ParseRegText(text, entries, error)) << error;
    ASSERT_EQ(entries.size(), 3u);

    EXPECT_EQ(appbox::RegistryStringValue(entries[1].data), L"C:\\App\\");
    EXPECT_EQ(appbox::RegistryStringValue(entries[2].data), L"a\"b");
}

TEST(UnitRegFile, ParsesKeyPathsSplitOverSeveralLines)
{
    std::vector<appbox::RegFileEntry> entries;
    std::string error;

    const std::wstring text = L"Windows Registry Editor Version 5.00\r\n"
                              L"[HKEY_CURRENT_USER\\Soft\\\r\n"
                              L"ware]\r\n"
                              L"\"Name\"=\"App\"\r\n";

    ASSERT_TRUE(appbox::ParseRegText(text, entries, error)) << error;
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].key_path, L"HKEY_CURRENT_USER\\Soft\\ware");
}

TEST(UnitRegFile, RejectsMalformedFiles)
{
    std::vector<appbox::RegFileEntry> entries;
    std::string error;

    EXPECT_FALSE(appbox::ParseRegText(L"", entries, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(appbox::ParseRegText(L"; only a comment\r\n", entries, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(appbox::ParseRegText(L"Windows Registry Editor Version 5.00\r\n"
                                      L"\"Name\"=\"App\"\r\n",
                                      entries, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(appbox::ParseRegText(L"Windows Registry Editor Version 5.00\r\n"
                                      L"[HKEY_DYN_DATA\\Software]\r\n",
                                      entries, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(appbox::ParseRegText(L"Windows Registry Editor Version 5.00\r\n"
                                      L"[HKEY_CURRENT_USER]\r\n"
                                      L"\"Name\"=hex(5):01\r\n",
                                      entries, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(appbox::ParseRegText(L"Windows Registry Editor Version 5.00\r\n"
                                      L"[HKEY_CURRENT_USER]\r\n"
                                      L"\"Name\"=\"unterminated\r\n",
                                      entries, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(appbox::ParseRegText(L"Windows Registry Editor Version 5.00\r\n"
                                      L"[HKEY_CURRENT_USER]\r\n"
                                      L"\"Name\"=hex:0\r\n",
                                      entries, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(appbox::ParseRegText(L"Windows Registry Editor Version 5.00\r\n"
                                      L"[HKEY_CURRENT_USER\r\n",
                                      entries, error));
    EXPECT_FALSE(error.empty());

    /* A failed parse must not leave entries behind. */
    EXPECT_TRUE(entries.empty());
}

TEST(UnitRegFile, LoadsUtf16AndUtf8Files)
{
    TempDir folder;

    const std::wstring text = L"Windows Registry Editor Version 5.00\r\n"
                              L"[HKEY_CURRENT_USER\\Software\\Vendor]\r\n"
                              L"\"Name\"=\"App\"\r\n";

    const auto utf16_path = folder.File(L"sample16.reg");
    WriteBytes(utf16_path, Utf16Le(text));

    const auto utf8_path = folder.File(L"sample8.reg");
    const std::string utf8_text = "Windows Registry Editor Version 5.00\r\n"
                                  "[HKEY_CURRENT_USER\\Software\\Vendor]\r\n"
                                  "\"Name\"=\"\xC3\x84pp\"\r\n";
    WriteBytes(utf8_path, std::vector<unsigned char>(utf8_text.begin(), utf8_text.end()));

    std::vector<appbox::RegFileEntry> entries;
    std::string error;

    ASSERT_TRUE(appbox::LoadRegFile(utf16_path.wstring(), entries, error)) << error;
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].key_path, L"HKEY_CURRENT_USER\\Software\\Vendor");

    entries.clear();
    error.clear();
    ASSERT_TRUE(appbox::LoadRegFile(utf8_path.wstring(), entries, error)) << error;
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(appbox::RegistryStringValue(entries[1].data), L"\u00C4pp");
}

TEST(UnitRegFile, LoadReportsAMissingFile)
{
    TempDir folder;

    std::vector<appbox::RegFileEntry> entries;
    std::string error;

    EXPECT_FALSE(appbox::LoadRegFile(folder.File(L"missing.reg").wstring(), entries, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(entries.empty());
}

TEST(UnitRegFile, MergeCreatesKeysAndValues)
{
    appbox::RegistryModel model;
    std::vector<appbox::RegFileEntry> entries;
    std::string error;

    ASSERT_TRUE(appbox::ParseRegText(kSampleText, entries, error)) << error;
    ASSERT_TRUE(appbox::MergeRegFile(model, entries, error)) << error;

    const auto* vendor = model.FindKey(L"HKEY_CURRENT_USER\\Software\\Vendor");
    ASSERT_NE(vendor, nullptr);
    EXPECT_EQ(vendor->values.size(), 11u);

    /* The key which the file created and removed again is gone. */
    EXPECT_EQ(model.FindKey(L"HKEY_CURRENT_USER\\Software\\Temp"), nullptr);

    const auto* legacy = model.FindKey(L"HKEY_LOCAL_MACHINE\\Software\\Legacy");
    ASSERT_NE(legacy, nullptr);
    ASSERT_EQ(legacy->values.size(), 1u);
    EXPECT_EQ(appbox::RegistryStringValue(legacy->values[0].data), L"C:\\App\\");
}

TEST(UnitRegFile, MergeKeepsTheIsolationModesOfTheModel)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Software\\Vendor", error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Name",
                               appbox::RegistryValueType::String, appbox::RegistryStringData(L"Old"), error))
        << error;
    ASSERT_TRUE(model.SetKeyIsolation(L"HKEY_CURRENT_USER\\Software\\Vendor",
                                      appbox::RegistryIsolation::Hide));
    ASSERT_TRUE(model.SetValueIsolation(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Name",
                                        appbox::RegistryIsolation::Full));

    std::vector<appbox::RegFileEntry> entries;
    ASSERT_TRUE(appbox::ParseRegText(L"Windows Registry Editor Version 5.00\r\n"
                                     L"[HKEY_CURRENT_USER\\Software\\Vendor]\r\n"
                                     L"\"Name\"=\"New\"\r\n"
                                     L"[HKEY_CURRENT_USER\\Software\\Vendor\\Child]\r\n",
                                     entries, error))
        << error;
    ASSERT_TRUE(appbox::MergeRegFile(model, entries, error)) << error;

    const auto* vendor = model.FindKey(L"HKEY_CURRENT_USER\\Software\\Vendor");
    ASSERT_NE(vendor, nullptr);

    /* The existing value is replaced, its isolation mode survives. */
    ASSERT_EQ(vendor->values.size(), 1u);
    EXPECT_EQ(appbox::RegistryStringValue(vendor->values[0].data), L"New");
    EXPECT_EQ(vendor->values[0].isolation, appbox::RegistryIsolation::Full);

    /* The isolation of the key survives as well. */
    EXPECT_EQ(vendor->isolation, appbox::RegistryIsolation::Hide);

    /* A key created by the merge follows the key it is created below. */
    ASSERT_EQ(vendor->children.size(), 1u);
    EXPECT_EQ(vendor->children[0].name, L"Child");
    EXPECT_EQ(vendor->children[0].isolation, appbox::RegistryIsolation::Hide);
}

TEST(UnitRegFile, MergeRemovesKeysAndValues)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Software\\Vendor\\App", error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Name",
                               appbox::RegistryValueType::String, {}, error))
        << error;

    std::vector<appbox::RegFileEntry> entries;
    ASSERT_TRUE(appbox::ParseRegText(L"Windows Registry Editor Version 5.00\r\n"
                                     L"[HKEY_CURRENT_USER\\Software\\Vendor]\r\n"
                                     L"\"Name\"=-\r\n"
                                     L"[-HKEY_CURRENT_USER\\Software\\Vendor\\App]\r\n"
                                     L"[-HKEY_CURRENT_USER\\Software\\Missing]\r\n",
                                     entries, error))
        << error;
    ASSERT_TRUE(appbox::MergeRegFile(model, entries, error)) << error;

    const auto* vendor = model.FindKey(L"HKEY_CURRENT_USER\\Software\\Vendor");
    ASSERT_NE(vendor, nullptr);
    EXPECT_TRUE(vendor->values.empty());
    EXPECT_TRUE(vendor->children.empty());
}

TEST(UnitRegFile, MergeRejectsTheRemovalOfARootKeyWithoutChangingTheModel)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Software", error)) << error;

    std::vector<appbox::RegFileEntry> entries;
    ASSERT_TRUE(appbox::ParseRegText(L"Windows Registry Editor Version 5.00\r\n"
                                     L"[-HKEY_CURRENT_USER]\r\n",
                                     entries, error))
        << error;

    error.clear();
    EXPECT_FALSE(appbox::MergeRegFile(model, entries, error));
    EXPECT_FALSE(error.empty());

    /* The rejected merge must not touch the model. */
    EXPECT_NE(model.FindKey(L"HKEY_CURRENT_USER"), nullptr);
    EXPECT_NE(model.FindKey(L"HKEY_CURRENT_USER\\Software"), nullptr);
}
