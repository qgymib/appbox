#include "probe/RegReadValues.hpp"
#include "probe/RegWriteValue.hpp"
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
 *    holds the value `Value` as well.
 * 2. The first run writes the value `Runtime` into the same key, which no
 *    package names.
 * 3. The second run reads both values.
 *
 * Expected:
 * 1. `Value` is the value of the package in both runs, because the hives of
 *    the packages are applied to the hive of the sandbox at every start.
 * 2. `Runtime` survives the second run, because an entry no package names
 *    keeps the state of the sandbox.
 */
TEST_F(E2E_Patch, RegistryStateBelowThePatchIsKept)
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

    /* The first run writes a value which no package names. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "Runtime";
        req.Data = "written";

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
        ASSERT_EQ(rsp.set_code, 0u);
        EXPECT_EQ(rsp.readback, "written");
    }

    /* The second run applies the package again and reads the state back. */
    {
        ProtocolRegReadValues::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Values = { "Value", "Runtime" };

        const auto rsp = ProbeRegReadValues.Call(req, GetCWD(), config).get<ProtocolRegReadValues::Rsp>();
        ASSERT_EQ(rsp.values.size(), 2u);

        /* The package is applied at every start. */
        EXPECT_EQ(rsp.values[0].query_code, 0u);
        EXPECT_EQ(rsp.values[0].text, "package");

        /* An entry no package names keeps the state of the previous run. */
        EXPECT_EQ(rsp.values[1].query_code, 0u);
        EXPECT_EQ(rsp.values[1].text, "written");
    }
}
