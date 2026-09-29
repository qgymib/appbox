#include "probe/RegReadValues.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "Random.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <cstdint>
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

/**
 * @brief Write a patch package into the patch directory of the case.
 * @param[in] cwd Working directory of the case.
 * @param[in] name File name of the package.
 * @param[in] registry Registry domain of the package.
 * @return true on success.
 */
bool WritePackage(const std::filesystem::path& cwd, const wchar_t* name, const PatchRegistry& registry)
{
    std::error_code ec;
    const auto      directory = cwd / appbox::layout::kPatchDirNameW;
    std::filesystem::create_directories(directory, ec);

    return WritePatchPackage(directory / name, {}, {}, registry);
}

} // namespace

/**
 * Condition:
 * 1. The hive of the archive holds the values `Value` and `Kept` of a key.
 * 2. `00-foo.zip` overrides `Value` and adds `Added`, `01-bar.zip` overrides
 *    `Value` and adds `AddedByBar`.
 * 3. The sandboxed process reads the four values.
 *
 * Expected:
 * 1. `Value` is the value of `01-bar.zip`, because the hives of the packages
 *    are applied in ascending order and the last one which names an entry wins.
 * 2. `Added` is the value of `00-foo.zip`, because a package overrides the
 *    entries it carries and not the entries of the layers below it.
 * 3. `Kept` is the value of the archive, which no package names.
 * 4. The hive of the resources is byte identical to the one the case built.
 */
TEST_F(E2E_Patch, RegistryHiveOfTheLastLayerWins)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\PatchRegistry_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    /* The base image of the virtual registry: the resources of the archive. */
    HiveBuilder app(GetCWD());
    app.SetValue(hive_key, L"Value", REG_SZ, StringData(L"archive"));
    app.SetValue(hive_key, L"Kept", REG_SZ, StringData(L"keep"));

    std::string error;
    ASSERT_TRUE(app.Write(error)) << error;

    const auto packed_hive = GetCWD() / appbox::layout::kAppDirNameW / appbox::layout::kRegistryDirNameW /
                             appbox::layout::kRegistryHiveFileNameW;

    std::vector<uint8_t> packed_before;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_before), static_cast<DWORD>(0));
    ASSERT_FALSE(packed_before.empty());

    /* The first package overrides the entry it names and adds one of its own. */
    PatchRegistry foo;
    foo.values.push_back(PatchRegistryValue{ hive_key, L"Value", REG_SZ, StringData(L"foo") });
    foo.values.push_back(PatchRegistryValue{ hive_key, L"Added", REG_SZ, StringData(L"foo") });
    ASSERT_TRUE(WritePackage(GetCWD(), L"00-foo.zip", foo));

    /* The second package overrides the entry both layers below it name. */
    PatchRegistry bar;
    bar.values.push_back(PatchRegistryValue{ hive_key, L"Value", REG_SZ, StringData(L"bar") });
    bar.values.push_back(PatchRegistryValue{ hive_key, L"AddedByBar", REG_SZ, StringData(L"bar") });
    ASSERT_TRUE(WritePackage(GetCWD(), L"01-bar.zip", bar));

    ProtocolRegReadValues::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Values = { "Value", "Added", "AddedByBar", "Kept" };

    const auto rsp = ProbeRegReadValues.Call(req, GetCWD(), config).get<ProtocolRegReadValues::Rsp>();
    ASSERT_EQ(rsp.values.size(), 4u);

    /* The last package which names the entry wins over the layers below it. */
    EXPECT_EQ(rsp.values[0].query_code, 0u);
    EXPECT_EQ(rsp.values[0].text, "bar");

    /* An entry only the earlier package names is the entry of that package. */
    EXPECT_EQ(rsp.values[1].query_code, 0u);
    EXPECT_EQ(rsp.values[1].text, "foo");

    EXPECT_EQ(rsp.values[2].query_code, 0u);
    EXPECT_EQ(rsp.values[2].text, "bar");

    /* An entry no package names is the entry of the archive. */
    EXPECT_EQ(rsp.values[3].query_code, 0u);
    EXPECT_EQ(rsp.values[3].text, "keep");

    /* The resources of the application were not modified. */
    std::vector<uint8_t> packed_after;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_after), static_cast<DWORD>(0));
    EXPECT_EQ(packed_after, packed_before);
}
