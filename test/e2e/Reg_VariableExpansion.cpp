#include "probe/RegReadValues.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/TestKnownFolder.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

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
 * @param[in] items Items of the list.
 * @return The UTF-16 data of the items, terminated by a second null character.
 */
std::vector<BYTE> MultiStringData(const std::vector<std::wstring>& items)
{
    std::wstring text;
    for (const auto& item : items)
    {
        text.append(item);
        text.push_back(L'\0');
    }
    text.push_back(L'\0');

    std::vector<BYTE> data(text.size() * sizeof(wchar_t));
    memcpy(data.data(), text.data(), data.size());
    return data;
}

/**
 * @brief Format the data of a value as a hexadecimal text.
 * @param[in] data Raw data of the value.
 * @return The data as a lower case hexadecimal text, without separators.
 */
std::string Hex(const std::vector<BYTE>& data)
{
    static const char kDigits[] = "0123456789abcdef";

    std::string text;
    text.reserve(data.size() * 2);
    for (const BYTE byte : data)
    {
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 0x0F]);
    }

    return text;
}

} // namespace

/**
 * Condition:
 * 1. The packed hive of the resources holds a `REG_SZ`, a `REG_EXPAND_SZ` and
 *    a `REG_MULTI_SZ` value whose text references a known folder of this
 *    machine with `%APPBOX:<NAME>%`, plus a `REG_DWORD` and a `REG_BINARY`
 *    value whose bytes spell the same reference.
 * 2. The sandboxed application reads every value of the key.
 *
 * Expected:
 * 1. The reference of a string type is replaced with the real path of the
 *    known folder of this machine, and every item of the list is expanded on
 *    its own.
 * 2. The types which carry no text keep their bytes, so only the string types
 *    of the registry carry a reference.
 * 3. The hive of the resources is byte identical afterwards: the archive keeps
 *    the reference and the expansion happens while the sandbox runs.
 */
TEST_F(E2E_Reg, VariableExpansion)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const std::wstring documents = GetKnownFolderPath(L"#Documents#", false);
    const std::wstring profile = GetKnownFolderPath(L"#USERPROFILE#", false);

    const std::wstring subkey = L"Software\\AppBoxTest\\VariableExpansion";
    const std::wstring key = L"HKEY_CURRENT_USER\\" + subkey;

    /* The bytes which both the `REG_SZ` and the `REG_BINARY` value carry. */
    const std::vector<BYTE> reference = StringData(L"%APPBOX:Documents%\\Foo");

    HiveBuilder builder(GetCWD());
    builder.SetValue(key, L"Sz", REG_SZ, StringData(L"%APPBOX:Documents%\\Foo\\Bar"));
    builder.SetValue(key, L"ExpandSz", REG_EXPAND_SZ, StringData(L"%APPBOX:USERPROFILE%\\Foo"));
    builder.SetValue(key, L"MultiSz", REG_MULTI_SZ,
                     MultiStringData(std::vector<std::wstring>{ L"%APPBOX:Documents%\\Foo", L"plain" }));
    builder.SetValue(key, L"Dword", REG_DWORD, std::vector<BYTE>{ 0x78, 0x56, 0x34, 0x12 });
    builder.SetValue(key, L"Binary", REG_BINARY, reference);
    builder.SetKeyIsolation(key, appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    const auto packed_hive = GetCWD() / appbox::layout::kAppDirNameW / appbox::layout::kRegistryDirNameW /
                             appbox::layout::kRegistryHiveFileNameW;

    std::vector<uint8_t> packed_before;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_before), static_cast<DWORD>(0));
    ASSERT_FALSE(packed_before.empty());

    ProtocolRegReadValues::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Values = { "Sz", "ExpandSz", "MultiSz", "Dword", "Binary" };

    const auto rsp = ProbeRegReadValues.Call(req, GetCWD(), config).get<ProtocolRegReadValues::Rsp>();
    ASSERT_EQ(rsp.values.size(), req.Values.size());

    for (const auto& answer : rsp.values)
    {
        ASSERT_EQ(answer.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(answer.query_code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    EXPECT_EQ(rsp.values[0].type, static_cast<DWORD>(REG_SZ));
    EXPECT_EQ(rsp.values[0].text, appbox::WideToUTF8(documents + L"\\Foo\\Bar"));

    EXPECT_EQ(rsp.values[1].type, static_cast<DWORD>(REG_EXPAND_SZ));
    EXPECT_EQ(rsp.values[1].text, appbox::WideToUTF8(profile + L"\\Foo"));

    /* Every item of the list is expanded on its own. */
    EXPECT_EQ(rsp.values[2].type, static_cast<DWORD>(REG_MULTI_SZ));
    ASSERT_EQ(rsp.values[2].items.size(), 2u);
    EXPECT_EQ(rsp.values[2].items[0], appbox::WideToUTF8(documents + L"\\Foo"));
    EXPECT_EQ(rsp.values[2].items[1], "plain");

    /* The types which carry no text keep their bytes. */
    EXPECT_EQ(rsp.values[3].type, static_cast<DWORD>(REG_DWORD));
    EXPECT_EQ(rsp.values[3].bytes, "78563412");
    EXPECT_TRUE(rsp.values[3].text.empty());

    EXPECT_EQ(rsp.values[4].type, static_cast<DWORD>(REG_BINARY));
    EXPECT_EQ(rsp.values[4].bytes, Hex(reference));
    EXPECT_TRUE(rsp.values[4].text.empty());

    /* The resources of the archive are read-only and keep the reference. */
    std::vector<uint8_t> packed_after;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_after), static_cast<DWORD>(0));
    EXPECT_EQ(packed_after, packed_before);
}
