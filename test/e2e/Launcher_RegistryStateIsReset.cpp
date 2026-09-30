#include "probe/RegReadValue.hpp"
#include "probe/RegWriteValue.hpp"
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
 * 1. The resources of the case carry a hive with the value `packed`.
 * 2. The first run writes the value `sandbox` into the same key.
 * 3. The state directory is deleted.
 * 4. The second run reads the value.
 *
 * Expected:
 * 1. The read of the second run returns `packed`: the state directory carries
 *    the whole state of the sandbox, so deleting it resets the sandbox to the
 *    registry the archive was packed with.
 * 2. The resources of the application were not modified.
 */
TEST_F(E2E_Launcher, RegistryStateIsReset)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\RegistryStateIsReset_" + appbox::UTF8ToWide(appbox::RandomString(8));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"TestValue", REG_SZ, StringData(L"packed"));
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey, appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    const auto packed_hive = GetCWD() / appbox::layout::kAppDirNameW / appbox::layout::kRegistryDirNameW /
                             appbox::layout::kRegistryHiveFileNameW;

    std::vector<uint8_t> packed_before;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_before), static_cast<DWORD>(0));
    ASSERT_FALSE(packed_before.empty());

    /* The first run seeds the state directory and writes its own value. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "TestValue";
        req.Data = "sandbox";

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.set_code, 0u);
        ASSERT_EQ(rsp.readback, "sandbox");
    }

    /* Deleting the state directory discards every modification. */
    {
        std::error_code ec;
        std::filesystem::remove_all(GetCWD() / appbox::layout::kStateDirNameW, ec);
        ASSERT_FALSE(ec);
        ASSERT_FALSE(std::filesystem::exists(GetCWD() / appbox::layout::kStateDirNameW));
    }

    /* The next run mounts the packed hive again. */
    {
        ProtocolRegReadValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "TestValue";

        const auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
        ASSERT_EQ(rsp.query_code, 0u);
        EXPECT_EQ(rsp.data, "packed");
    }

    /* The resources of the application were not modified. */
    std::vector<uint8_t> packed_after;
    ASSERT_EQ(ReadFileFull(packed_hive.wstring(), packed_after), static_cast<DWORD>(0));
    EXPECT_EQ(packed_after, packed_before);
}
