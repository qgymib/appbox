#include "probe/RegDeleteValue.hpp"
#include "probe/RegReadValues.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <set>
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

} // namespace

/**
 * Condition:
 * 1. The image of the run (the registry of the archive) holds the key with the
 *    values `Value` and `Kept`; the state of the sandbox holds neither.
 * 2. The sandboxed process deletes the value `Value`.
 * 3. The next run reads both values, which rebuilds the image from the
 *    resources of the archive.
 *
 * Expected:
 * 1. The delete succeeds, although only the image holds the value: the state
 *    hive records the value as deleted (a whiteout).
 * 2. The value is gone from the view of the run which deleted it: the read and
 *    the enumeration report it as missing while `Kept` stays visible.
 * 3. The value stays invisible in the next run, because the marker hides the
 *    value of the rebuilt image as well.
 */
TEST_F(E2E_Reg, DeleteValue_ImageValueWhiteout)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\DeleteValue_Image_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    HiveBuilder builder(GetCWD());
    builder.SetValue(hive_key, L"Value", REG_SZ, StringData(L"image"));
    builder.SetValue(hive_key, L"Kept", REG_SZ, StringData(L"kept"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    {
        ProtocolRegDeleteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "Value";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegDeleteValue>(req);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

        const std::set<std::string> names(rsp.names.begin(), rsp.names.end());
        EXPECT_EQ(names.count("Value"), 0u);
        EXPECT_EQ(names.count("Kept"), 1u);
    }

    /* The next run rebuilds the image and keeps the marker. */
    {
        ProtocolRegReadValues::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Values = { "Value", "Kept" };

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegReadValues>(req);
        ASSERT_EQ(rsp.values.size(), 2u);

        EXPECT_NE(rsp.values[0].query_code, static_cast<DWORD>(0));
        EXPECT_EQ(rsp.values[1].query_code, 0u);
        EXPECT_EQ(rsp.values[1].text, "kept");
    }
}
