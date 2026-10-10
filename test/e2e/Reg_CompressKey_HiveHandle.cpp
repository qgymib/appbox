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
 * 2. Call `NtCompressKey` on that handle inside the sandbox.
 *
 * Expected:
 * 1. The call is forwarded: the hive is the write layer of the sandbox, so the
 *    object of the call is the hive file of the sandbox. The kernel refuses the
 *    call with `STATUS_PRIVILEGE_NOT_HELD`, because the token of an ordinary
 *    process does not carry the privilege the call needs; the isolation itself
 *    does not refuse a handle of the hive.
 * 2. The real registry keeps the key and its value.
 */
TEST_F(E2E_Reg, CompressKey_HiveHandle)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\CompressKey_HiveHandle_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    ProtocolRegKeyApi::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Api = "NtCompressKey";
    req.Mode = "hive_handle";

    const auto rsp = ProbeRegKeyApi.Call(req, GetCWD(), config).get<ProtocolRegKeyApi::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));

    /* The kernel answers the call of the hive: the isolation does not refuse it. */
    EXPECT_EQ(rsp.api_code, static_cast<DWORD>(STATUS_PRIVILEGE_NOT_HELD));

    EXPECT_EQ(real_key.GetString(L"HostValue"), L"host");
}
