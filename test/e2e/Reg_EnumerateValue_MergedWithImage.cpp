#include "probe/RegEnumValue.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <algorithm>
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
 *    values `Alpha` and `Beta`.
 * 2. The sandboxed process overwrites `Alpha` and adds the value `Gamma`, which
 *    copies the key up into the state hive.
 * 3. The sandboxed process enumerates the values of the key.
 *
 * Expected:
 * 1. The enumeration reports the three names, because the values of the image
 *    and of the state hive are merged.
 * 2. `Alpha` is the value of the state hive, while `Beta` keeps the value of the
 *    image.
 * 3. The count of the key reports three values as well.
 */
TEST_F(E2E_Reg, EnumerateValue_MergedWithImage)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\EnumValue_Image_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    HiveBuilder builder(GetCWD());
    builder.SetValue(hive_key, L"Alpha", REG_SZ, StringData(L"image"));
    builder.SetValue(hive_key, L"Beta", REG_SZ, StringData(L"b"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    for (const auto& entry : { std::make_pair("Alpha", "sandbox"), std::make_pair("Gamma", "g") })
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = entry.first;
        req.Data = entry.second;

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegWriteValue>(req);
        ASSERT_EQ(rsp.create_code, 0u);
        ASSERT_EQ(rsp.set_code, 0u);
        ASSERT_EQ(rsp.readback, entry.second);
    }

    {
        ProtocolRegEnumValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegEnumValue>(req);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.count_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.enum_code, 0u);
        EXPECT_EQ(rsp.value_count, 3u);

        for (const char* name : { "Alpha", "Beta", "Gamma" })
        {
            EXPECT_NE(std::find(rsp.names.begin(), rsp.names.end(), name), rsp.names.end()) << name;
        }

        EXPECT_EQ(rsp.values.at("Alpha"), "sandbox");
        EXPECT_EQ(rsp.values.at("Beta"), "b");
        EXPECT_EQ(rsp.values.at("Gamma"), "g");
    }
}
