#include "probe/RegReadValues.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/RealHkcuKey.hpp"
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

} // namespace

/**
 * Condition:
 * 1. The hive of the archive holds the value `Value` of a key and the
 *    isolation file of the archive hides the value `HostHidden` of the host.
 * 2. `00-foo.zip` carries a file of the filesystem domain and no resource of
 *    the registry domain at all.
 * 3. The sandboxed process reads the two values of the key.
 *
 * Expected:
 * 1. `Value` is the value of the archive, because a package which carries no
 *    hive keeps the hive of the layers below it.
 * 2. `HostHidden` does not exist, because a package which carries no isolation
 *    file keeps the modes of the layers below it.
 * 3. The hive of the resources is byte identical to the one the case built.
 */
TEST_F(E2E_Patch, RegistryWithoutResourcesKeepsTheApp)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", { FsDir(L"#USERPROFILE#", {}) }) })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\PatchRegistryMissing_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    RealHkcuKey host(subkey);
    ASSERT_NE(host.get(), nullptr);
    ASSERT_TRUE(host.SetString(L"HostHidden", L"host"));

    HiveBuilder app(GetCWD());
    app.SetValue(hive_key, L"Value", REG_SZ, StringData(L"archive"));
    app.SetValueIsolation(hive_key, L"HostHidden", appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(app.Write(error)) << error;

    const auto packed_hive = GetCWD() / appbox::layout::kAppDirNameW / appbox::layout::kRegistryDirNameW /
                             appbox::layout::kRegistryHiveFileNameW;

    std::vector<uint8_t> packed_before;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_before), static_cast<DWORD>(0));
    ASSERT_FALSE(packed_before.empty());

    /* The package carries filesystem content only. */
    std::error_code ec;
    const auto      patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    std::filesystem::create_directories(patch_dir, ec);
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"00-foo.zip",
                                  {
                                      PatchFile{ L"#USERPROFILE#\\AppBoxTest\\patch.txt", "patch" }
    }));

    ProtocolRegReadValues::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Values = { "Value", "HostHidden" };

    const auto rsp = ProbeRegReadValues.Call(req, GetCWD(), config).get<ProtocolRegReadValues::Rsp>();
    ASSERT_EQ(rsp.values.size(), 2u);

    /* The hive of the archive stays the hive of the run. */
    EXPECT_EQ(rsp.values[0].query_code, 0u);
    EXPECT_EQ(rsp.values[0].text, "archive");

    /* The modes of the archive stay the modes of the run. */
    EXPECT_EQ(rsp.values[1].query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The resources of the application were not modified. */
    std::vector<uint8_t> packed_after;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_after), static_cast<DWORD>(0));
    EXPECT_EQ(packed_after, packed_before);
}
