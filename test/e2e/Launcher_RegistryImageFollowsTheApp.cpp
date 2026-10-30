#include "probe/RegReadValues.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher;
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
 * 1. The resources of the case carry a hive which holds the value `Value` of a
 *    key.
 * 2. The first run writes the value `Runtime` of the same key inside the
 *    sandbox.
 * 3. The resources of the case are packed again with another value of `Value`,
 *    which is what an archive which was repacked between two runs looks like.
 * 4. The second run reads both values.
 *
 * Expected:
 * 1. `Value` is the value of the new resources, because `app` is the layer
 *    below the state of the sandbox and the content of the archive is applied
 *    at every start (see [PatchLayer.md](../docs/PatchLayer.md)).
 * 2. `Runtime` keeps the value of the first run, because the state of the
 *    sandbox is the highest layer of the view and survives a new start.
 */
TEST_F(E2E_Launcher, RegistryImageFollowsTheApp)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\RegistryImage_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    {
        HiveBuilder app(GetCWD());
        app.SetValue(hive_key, L"Value", REG_SZ, StringData(L"v1"));

        std::string error;
        ASSERT_TRUE(app.Write(error)) << error;
    }

    /* The first run writes a value which no layer of the archive holds. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "Runtime";
        req.Data = "kept";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegWriteValue>(req);
        ASSERT_EQ(rsp.create_code, 0u);
        ASSERT_EQ(rsp.set_code, 0u);
        EXPECT_EQ(rsp.readback, "kept");
    }

    /* The application is packed again between the two runs. */
    {
        HiveBuilder app(GetCWD());
        app.SetValue(hive_key, L"Value", REG_SZ, StringData(L"v2"));

        std::string error;
        ASSERT_TRUE(app.Write(error)) << error;
    }

    {
        ProtocolRegReadValues::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Values = { "Value", "Runtime" };

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegReadValues>(req);
        ASSERT_EQ(rsp.values.size(), 2u);

        /* The content of the archive is applied at every start. */
        EXPECT_EQ(rsp.values[0].query_code, 0u);
        EXPECT_EQ(rsp.values[0].text, "v2");

        /* The state of the sandbox survives the start. */
        EXPECT_EQ(rsp.values[1].query_code, 0u);
        EXPECT_EQ(rsp.values[1].text, "kept");
    }
}
