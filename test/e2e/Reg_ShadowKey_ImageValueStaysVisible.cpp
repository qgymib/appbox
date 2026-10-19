#include "probe/RegReadValues.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
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
 *    values `Value` and `Other`.
 * 2. The sandboxed process writes the value `Value` of the key, which copies the
 *    key up into the state hive.
 * 3. The sandboxed process reads both values.
 *
 * Expected:
 * 1. `Value` is the value of the state hive, because the state of the sandbox is
 *    the highest layer of the view.
 * 2. `Other` is still the value of the image, because the copied up key is
 *    merged with the image and only the value the write named is shadowed.
 */
TEST_F(E2E_Reg, ShadowKey_ImageValueStaysVisible)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\ShadowKey_Image_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    HiveBuilder builder(GetCWD());
    builder.SetValue(hive_key, L"Value", REG_SZ, StringData(L"image"));
    builder.SetValue(hive_key, L"Other", REG_SZ, StringData(L"kept"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* The write of the sandbox copies the key of the image up. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "Value";
        req.Data = "sandbox";

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
        /* The key exists in the view, because the image holds it. */
        EXPECT_EQ(rsp.disposition, static_cast<DWORD>(REG_OPENED_EXISTING_KEY));
        ASSERT_EQ(rsp.set_code, 0u);
        EXPECT_EQ(rsp.readback, "sandbox");
    }

    {
        ProtocolRegReadValues::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Values = { "Value", "Other" };

        const auto rsp = ProbeRegReadValues.Call(req, GetCWD(), config).get<ProtocolRegReadValues::Rsp>();
        ASSERT_EQ(rsp.values.size(), 2u);

        EXPECT_EQ(rsp.values[0].query_code, 0u);
        EXPECT_EQ(rsp.values[0].text, "sandbox");

        /* The value of the image stays visible through the copied up key. */
        EXPECT_EQ(rsp.values[1].query_code, 0u);
        EXPECT_EQ(rsp.values[1].text, "kept");
    }
}
