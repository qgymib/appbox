#include "probe/RegReadValues.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "Random.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Patch;
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
 * 1. The hive of the archive holds the value `Value` of a key, `01-bar.zip`
 *    holds the value `Value` of the same key as well.
 * 2. The first run writes `Value` inside the sandbox, so the state directory
 *    of the sandbox holds the value the packaged application wrote.
 * 3. The second run reads `Value`.
 *
 * Expected:
 * 1. The second run reports the value of the first run, because the state of
 *    the sandbox is the highest layer of the view: `data` overrides `patch`,
 *    which overrides `app` (see [PatchLayer.md](../docs/PatchLayer.md)).
 * 2. The package is not applied on top of the state of the sandbox at the
 *    next start, so a write of the application is not reset by a package which
 *    names the same entry.
 */
TEST_F(E2E_Patch, RegistryStateOverridesThePatch)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\PatchRegistryState_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    HiveBuilder app(GetCWD());
    app.SetValue(hive_key, L"Value", REG_SZ, StringData(L"archive"));

    std::string error;
    ASSERT_TRUE(app.Write(error)) << error;

    PatchRegistry bar;
    bar.values.push_back(PatchRegistryValue{ hive_key, L"Value", REG_SZ, StringData(L"package") });

    std::error_code ec;
    const auto      patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    std::filesystem::create_directories(patch_dir, ec);
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"01-bar.zip", {}, {}, bar));

    /* The first run writes the value the package names as well. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "Value";
        req.Data = "runtime";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegWriteValue>(req);
        ASSERT_EQ(rsp.create_code, 0u);
        ASSERT_EQ(rsp.set_code, 0u);
        EXPECT_EQ(rsp.readback, "runtime");
    }

    /* The second run reads the value of the state, not the value of the package. */
    {
        ProtocolRegReadValues::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Values = { "Value" };

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegReadValues>(req);
        ASSERT_EQ(rsp.values.size(), 1u);

        EXPECT_EQ(rsp.values[0].query_code, 0u);
        EXPECT_EQ(rsp.values[0].text, "runtime");
    }
}
