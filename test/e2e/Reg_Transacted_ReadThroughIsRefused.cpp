#include "probe/RegTransacted.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The key exists in the real HKCU with the value `HostValue` and the sandbox
 *    hive does not hold it, so the key keeps the default mode `WriteCopy`.
 * 2. Open the key inside a transaction of the probe, once read only
 *    (`NtOpenKeyTransacted`) and once with write access
 *    (`NtOpenKeyTransactedEx`), which is the call the isolation copies a key up
 *    for.
 *
 * Expected:
 * 1. The read only call is refused with `STATUS_NOT_SUPPORTED`: the hive layer
 *    does not hold the key and the only handle the isolation could hand out is a
 *    handle of the host layer, which would enlist the real hive into the
 *    transaction of the sandboxed process. The isolation never does that, so the
 *    read through of a transacted open fails closed.
 * 2. The write access call is refused with `STATUS_RM_NOT_ACTIVE`: the copy-up
 *    creates the shadow key inside the sandbox hive, and the hive does not
 *    support transactions.
 * 3. The real registry is unchanged in both cases: the host key keeps its value
 *    and it never receives the value of the probe. A copy-up which would have
 *    used the host key would have landed there.
 * 4. The key is still readable inside the sandbox through the plain open, which
 *    is the read through of the isolation: the refusals cover the transacted
 *    calls alone.
 */
TEST_F(E2E_Reg, Transacted_ReadThroughIsRefused)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\Transacted_ReadThrough_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    ProtocolRegTransacted::Req read_req;
    read_req.Api = "open";
    read_req.Access = "read";
    read_req.Key = appbox::WideToUTF8(subkey);
    read_req.Value = "HostValue";
    read_req.End = "close";

    const auto read_rsp = ProbeRegTransacted.Call(read_req, GetCWD(), config).get<ProtocolRegTransacted::Rsp>();
    ASSERT_EQ(read_rsp.mount_code, static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_EQ(read_rsp.tx_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(read_rsp.call_code, static_cast<DWORD>(STATUS_NOT_SUPPORTED));

    /* The plain open of the same key keeps its read through. */
    EXPECT_EQ(read_rsp.after_open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(read_rsp.after_read_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(read_rsp.after, "host");

    ProtocolRegTransacted::Req write_req;
    write_req.Api = "open_ex";
    write_req.Access = "write";
    write_req.Key = appbox::WideToUTF8(subkey);
    write_req.Value = "TestValue";
    write_req.Data = "sandbox";
    write_req.End = "commit";

    const auto write_rsp = ProbeRegTransacted.Call(write_req, GetCWD(), config).get<ProtocolRegTransacted::Rsp>();
    ASSERT_EQ(write_rsp.mount_code, static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_EQ(write_rsp.tx_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(write_rsp.call_code, static_cast<DWORD>(STATUS_RM_NOT_ACTIVE));

    /* The real registry is unchanged: the host key keeps its own value and the
     * value of the sandbox never reached it. */
    EXPECT_EQ(appbox::WideToUTF8(real_key.GetString(L"HostValue")), "host");

    HKEY key = nullptr;
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    EXPECT_NE(RegQueryValueExW(key, L"TestValue", nullptr, nullptr, nullptr, nullptr),
              static_cast<LONG>(ERROR_SUCCESS));
    RegCloseKey(key);
}
