#include "probe/RegTransacted.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The sandbox hive does not hold the key and the isolation file does not
 *    mention it, so the key keeps the default mode `WriteCopy`.
 * 2. Create the key inside a transaction of the probe
 *    (`NtCreateKeyTransacted`) and write a value through the handle the call
 *    returned.
 *
 * Expected:
 * 1. The call is refused with `STATUS_RM_NOT_ACTIVE`: a create of a key of the
 *    view lands in the sandbox hive, and an application hive does not support
 *    transactions, so the kernel refuses the transacted create of the hive.
 *    The isolation reports that failure instead of answering the call with a
 *    plain (non transacted) create, because the rollback of the caller would
 *    silently stop working otherwise.
 * 2. The real registry never receives the key: the transacted entry points of
 *    the isolation redirect the call into the hive, so a call which the kernel
 *    would have served against the real HKCU without the isolation leaves the
 *    host untouched.
 */
TEST_F(E2E_Reg, Transacted_CreateIsRefused)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\Transacted_Create_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::string expected = "Transacted";

    ProtocolRegTransacted::Req req;
    req.Api = "create";
    req.Access = "write";
    req.Key = appbox::WideToUTF8(subkey);
    req.Value = "TestValue";
    req.Data = expected;
    req.End = "commit";

    const auto rsp = ProbeRegTransacted.Call(req, GetCWD(), config).get<ProtocolRegTransacted::Rsp>();
    ASSERT_EQ(rsp.mount_code, static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_EQ(rsp.tx_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.call_code, static_cast<DWORD>(STATUS_RM_NOT_ACTIVE));

    /* The view does not hold the key either: the create did not land anywhere. */
    EXPECT_EQ(rsp.after_open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The real registry never received the key. */
    HKEY key = nullptr;
    EXPECT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key),
              static_cast<LONG>(ERROR_FILE_NOT_FOUND));
    if (key != nullptr)
    {
        RegCloseKey(key);
    }
}
