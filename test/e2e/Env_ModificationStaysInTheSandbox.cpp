#include "probe/EnvironmentWrite.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds `APPBOX_ENV_HOST=foo`.
 * 2. The isolation file lists the variable with the value `bar`.
 * 3. The sandboxed application stores the variable with the value `changed` and
 *    removes the variable `APPBOX_ENV_GONE`, which the host holds.
 *
 * Expected:
 * 1. The application reads `changed` back, so the write reached its own
 *    environment.
 * 2. The variable which was removed is no longer part of the environment.
 * 3. The environment of the host still carries `foo`: the write of the
 *    application never left the sandbox.
 */
TEST_F(E2E_Env, ModificationStaysInTheSandbox)
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

    ASSERT_TRUE(WriteEnvironmentIsolationFile(GetCWD(),
                                              {
                                                  { L"APPBOX_ENV_HOST", L"bar", appbox::EnvironmentIsolation::WriteCopy,
                                                   appbox::EnvironmentMergeMode::Replace, L"" }
    }));

    ProtocolEnvironmentWrite::Req req;
    req.names = { "APPBOX_ENV_HOST" };
    req.values = { "changed" };
    req.remove = { "APPBOX_ENV_GONE" };
    req.read = { "APPBOX_ENV_HOST", "APPBOX_ENV_GONE" };

    const auto rsp = ProbeEnvironmentWrite.Call(req, GetCWD(), config).get<ProtocolEnvironmentWrite::Rsp>();
    ASSERT_EQ(rsp.stored.size(), 1u);
    EXPECT_TRUE(rsp.stored[0]);
    ASSERT_EQ(rsp.removed.size(), 1u);
    EXPECT_TRUE(rsp.removed[0]);

    ASSERT_EQ(rsp.values.size(), 2u);
    ASSERT_EQ(rsp.found.size(), 2u);
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "changed");
    EXPECT_FALSE(rsp.found[1]);

    /* The environment of the test process is the environment of the host. */
    EXPECT_EQ(host.Value(), L"foo");
    EXPECT_EQ(gone.Value(), L"hosted");

    ASSERT_TRUE(tree.Verify());
}
