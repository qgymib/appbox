#include "probe/EnvironmentRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds `APPBOX_ENV_HOST=foo` and `APPBOX_ENV_OTHER=keep`.
 * 2. The isolation file lists `APPBOX_ENV_HOST` with the isolation mode `Full`
 *    and the value `bar`.
 * 3. The sandboxed application reads both variables.
 *
 * Expected:
 * 1. The application sees `bar` for the configured variable, so the value of
 *    the host is invisible.
 * 2. The application sees the value of the host for the variable which the
 *    isolation file does not list.
 * 3. The ANSI entry point reports the same view as the wide one.
 */
TEST_F(E2E_Env, Full_HidesTheHostValue)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const HostEnvironmentVariable host(L"APPBOX_ENV_HOST", L"foo");
    const HostEnvironmentVariable other(L"APPBOX_ENV_OTHER", L"keep");

    ASSERT_TRUE(
        WriteEnvironmentIsolationFile(GetCWD(), {
                                                    { L"APPBOX_ENV_HOST", L"bar", appbox::EnvironmentIsolation::Full,
                                                     appbox::EnvironmentMergeMode::Replace, L"" }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_HOST", "APPBOX_ENV_OTHER" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 2u);
    ASSERT_EQ(rsp.found.size(), 2u);
    ASSERT_EQ(rsp.ansi_values.size(), 2u);

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "bar");
    EXPECT_EQ(rsp.ansi_values[0], "bar");

    EXPECT_TRUE(rsp.found[1]);
    EXPECT_EQ(rsp.values[1], "keep");

    ASSERT_TRUE(tree.Verify());
}
