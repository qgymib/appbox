#include "probe/EnvironmentRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds `APPBOX_ENV_HOST=foo` and `APPBOX_ENV_GONE=hosted`.
 * 2. The state directory of the sandbox carries the modifications of an earlier
 *    run: `APPBOX_ENV_HOST` is stored with `kept` and `APPBOX_ENV_GONE` is
 *    removed.
 * 3. The sandboxed application reads both variables.
 *
 * Expected:
 * 1. The application sees `kept` for the stored variable.
 * 2. The application does not see the variable which was removed.
 */
TEST_F(E2E_Env, StateFileIsApplied)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const HostEnvironmentVariable host(L"APPBOX_ENV_HOST", L"foo");
    const HostEnvironmentVariable gone(L"APPBOX_ENV_GONE", L"hosted");

    ASSERT_TRUE(WriteEnvironmentStateFile(
        GetCWD(), {
                      { L"APPBOX_ENV_HOST", L"kept", false },
                      { L"APPBOX_ENV_GONE", L"",     true  }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_HOST", "APPBOX_ENV_GONE" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 2u);
    ASSERT_EQ(rsp.found.size(), 2u);

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "kept");

    EXPECT_FALSE(rsp.found[1]);

    ASSERT_TRUE(tree.Verify());
}
