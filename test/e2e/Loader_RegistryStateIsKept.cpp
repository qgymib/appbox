#include "probe/RegReadValue.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
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
 * 1. The resources of the case carry a hive with the value `packed`.
 * 2. The first run writes the value `sandbox` into the same key.
 * 3. The second run reads the value.
 *
 * Expected:
 * 1. The write of the first run lands in the state directory.
 * 2. The read of the second run returns `sandbox`: the loader seeds the hive
 *    only while the state directory carries none, so the modifications of an
 *    earlier run survive the next one.
 */
TEST_F(E2E_Loader, RegistryStateIsKept)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\RegistryStateIsKept_" + appbox::UTF8ToWide(appbox::RandomString(8));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"TestValue", REG_SZ, StringData(L"packed"));
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey, appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

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

    ASSERT_TRUE(std::filesystem::exists(GetCWD() / appbox::layout::kStateDirNameW / appbox::layout::kRegistryDirNameW /
                                        appbox::layout::kRegistryHiveFileNameW));

    /* The second run mounts the hive of the first run. */
    {
        ProtocolRegReadValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "TestValue";

        const auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
        ASSERT_EQ(rsp.query_code, 0u);
        EXPECT_EQ(rsp.data, "sandbox");
    }
}
