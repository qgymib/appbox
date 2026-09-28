#include "probe/RegReadValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "Random.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Loader;
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
 * 1. The resources of the case carry a hive with a value; the state directory
 *    of the sandbox does not exist at all.
 * 2. Read the value inside the sandbox.
 *
 * Expected:
 * 1. The read returns the value of the packed hive, because the loader seeded
 *    the hive into the state directory which it created at run time.
 * 2. The state directory carries the hive the sandbox mounted.
 * 3. The hive of the resources is byte identical to the one the case built, so
 *    the resources of the application were not modified.
 */
TEST_F(E2E_Loader, RegistryStateIsSeeded)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\RegistryStateIsSeeded_" + appbox::UTF8ToWide(appbox::RandomString(8));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"TestValue", REG_SZ, StringData(L"packed"));
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey, appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    const auto packed_hive = GetCWD() / appbox::layout::kAppDirNameW / appbox::layout::kRegistryDirNameW /
                             appbox::layout::kRegistryHiveFileNameW;
    const auto state_hive = GetCWD() / appbox::layout::kStateDirNameW / appbox::layout::kRegistryDirNameW /
                            appbox::layout::kRegistryHiveFileNameW;

    std::vector<uint8_t> packed_before;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_before), static_cast<DWORD>(0));
    ASSERT_FALSE(packed_before.empty());

    /* The state of the sandbox does not exist before the first run. */
    ASSERT_FALSE(std::filesystem::exists(GetCWD() / appbox::layout::kStateDirNameW));
    ASSERT_FALSE(std::filesystem::exists(state_hive));

    ProtocolRegReadValue::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Value = "TestValue";

    const auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
    ASSERT_EQ(rsp.open_code, 0u);
    ASSERT_EQ(rsp.query_code, 0u);
    EXPECT_EQ(rsp.data, "packed");

    /* The sandbox mounts a copy of the packed hive. */
    ASSERT_TRUE(std::filesystem::exists(state_hive));
    EXPECT_GT(std::filesystem::file_size(state_hive), 0u);

    /* The resources of the application were not modified. */
    std::vector<uint8_t> packed_after;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_after), static_cast<DWORD>(0));
    EXPECT_EQ(packed_after, packed_before);
}
