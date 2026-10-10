#include "probe/RegKeyApi.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The key and the value `HostValue` exist in the real HKCU and the sandbox
 *    hive does not hold the key, so a write access open of the key is copied up
 *    into the hive and the caller holds a handle of the hive layer.
 * 2. Call `NtSetInformationKey(KeyWriteTimeInformation)` on that handle inside
 *    the sandbox, which writes the fixed time of `kProbeWriteTime`.
 *
 * Expected:
 * 1. The call is forwarded: the hive is the write layer of the sandbox, so the
 *    key of the hive carries the time the probe wrote and the sandbox observes
 *    that time through its handle.
 * 2. The real registry keeps the last write time of the key, so the
 *    modification landed in the sandbox.
 */
TEST_F(E2E_Reg, SetInformationKey_HiveHandle)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey =
        L"Software\\AppBoxTest\\SetInformationKey_HiveHandle_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    const uint64_t host_before = real_key.LastWriteTime();
    ASSERT_NE(host_before, 0u);

    ProtocolRegKeyApi::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Api = "NtSetInformationKey";
    req.Mode = "hive_handle";

    const auto rsp = ProbeRegKeyApi.Call(req, GetCWD(), config).get<ProtocolRegKeyApi::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));

    EXPECT_EQ(rsp.api_code, static_cast<DWORD>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.write_time_after, kProbeWriteTime);

    /* The modification did not reach the real registry. */
    EXPECT_EQ(real_key.LastWriteTime(), host_before);
    EXPECT_EQ(real_key.GetString(L"HostValue"), L"host");
}
