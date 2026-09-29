#include "probe/EnvironmentRead.hpp"
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
 * 3. A first run of the sandboxed application stores the variable with the
 *    value `changed`, a second run reads it.
 *
 * Expected:
 * 1. The second run sees `changed`: the state directory of the sandbox carries
 *    the modification of the first run.
 * 2. The value of the host is still `foo`.
 */
TEST_F(E2E_Env, StateIsKeptAcrossRuns)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const HostEnvironmentVariable host(L"APPBOX_ENV_HOST", L"foo");

    ASSERT_TRUE(WriteEnvironmentIsolationFile(GetCWD(),
                                              {
                                                  { L"APPBOX_ENV_HOST", L"bar", appbox::EnvironmentIsolation::WriteCopy,
                                                   appbox::EnvironmentMergeMode::Replace, L"" }
    }));

    ProtocolEnvironmentWrite::Req write;
    write.names = { "APPBOX_ENV_HOST" };
    write.values = { "changed" };
    write.read = { "APPBOX_ENV_HOST" };

    const auto written = ProbeEnvironmentWrite.Call(write, GetCWD(), config).get<ProtocolEnvironmentWrite::Rsp>();
    ASSERT_EQ(written.values.size(), 1u);
    EXPECT_EQ(written.values[0], "changed");

    ProtocolEnvironmentRead::Req read;
    read.names = { "APPBOX_ENV_HOST" };

    const auto rsp = ProbeEnvironmentRead.Call(read, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 1u);
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "changed");

    EXPECT_EQ(host.Value(), L"foo");

    ASSERT_TRUE(tree.Verify());
}
