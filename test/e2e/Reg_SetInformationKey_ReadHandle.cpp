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
 * 2. Call `NtSetInformationKey(KeyWriteTimeInformation)` on that handle inside
 *    the sandbox, which asks for the fixed time of `kProbeWriteTime`.
 *
 * Expected:
 * 1. The call is refused with `STATUS_ACCESS_DENIED`: the object of the host
 *    layer is not the object of the sandbox, so the isolation never lets the
 *    call reach the real registry. A handle of the host layer carries read
 *    rights only, which the kernel refuses for this class as well, so the case
 *    pins that the key of the host stays untouched and that the isolation is
 *    the layer which answers the call.
 * 2. The last write time of the real key is unchanged.
 */
TEST_F(E2E_Reg, SetInformationKey_ReadHandle)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey =
        L"Software\\AppBoxTest\\SetInformationKey_ReadHandle_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    const uint64_t host_before = real_key.LastWriteTime();
    ASSERT_NE(host_before, 0u);

    ProtocolRegKeyApi::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Api = "NtSetInformationKey";
    req.Mode = "read_handle";

    const auto rsp = ProbeRegKeyApi.Call(req, GetCWD(), config).get<ProtocolRegKeyApi::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));

    EXPECT_EQ(rsp.api_code, static_cast<DWORD>(STATUS_ACCESS_DENIED));

    /* The view of the sandbox is the host key, and the host key is untouched. */
    EXPECT_EQ(rsp.write_time_before, host_before);
    EXPECT_EQ(rsp.write_time_after, host_before);
    EXPECT_EQ(real_key.LastWriteTime(), host_before);
    EXPECT_EQ(real_key.GetString(L"HostValue"), L"host");
}
