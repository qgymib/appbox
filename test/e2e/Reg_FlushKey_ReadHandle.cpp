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
 * 1. The key and the value `HostValue` exist in the real HKCU only, so a read
 *    access open of the key is answered by the real registry (the read through)
 *    and the caller holds a handle of the host layer.
 * 2. Call `NtFlushKey` on that handle inside the sandbox.
 *
 * Expected:
 * 1. The call is refused with `STATUS_ACCESS_DENIED`: the flush names the hive
 *    file which holds the key, and the hive of the host is not the state of the
 *    sandbox, so the isolation never lets the sandbox write it.
 * 2. The real registry keeps the key and its value.
 *
 * Before the isolation answered this call itself, the call was forwarded to the
 * host layer and reported `STATUS_SUCCESS`: the sandbox flushed the hive file
 * of the host. This case pins that the isolation is the layer which answers the
 * call.
 */
TEST_F(E2E_Reg, FlushKey_ReadHandle)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\FlushKey_ReadHandle_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    ProtocolRegKeyApi::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Api = "NtFlushKey";
    req.Mode = "read_handle";

    const auto rsp = ProbeRegKeyApi.Call(req, GetCWD(), config).get<ProtocolRegKeyApi::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));

    EXPECT_EQ(rsp.api_code, static_cast<DWORD>(STATUS_ACCESS_DENIED));

    EXPECT_EQ(real_key.GetString(L"HostValue"), L"host");
}
