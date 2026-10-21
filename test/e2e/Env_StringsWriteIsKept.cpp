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
 * 1. The host holds `APPBOX_ENV_HOST=foo` and the isolation file lists the
 *    variable with the value `bar`.
 * 2. A first run replaces the whole environment with `SetEnvironmentStringsW`,
 *    which is the Win32 entry point of the block replacement and which the
 *    runtime of the operating system answers through
 *    `RtlSetEnvironmentStrings`.
 * 3. A second run reads the variable.
 *
 * Expected:
 * 1. The second run sees `changed`: the block which replaced the environment of
 *    the process was kept in the state of the sandbox.
 * 2. The environment of the host still carries `foo`.
 */
TEST_F(E2E_Env, StringsWriteIsKept)
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
    write.entry = ProtocolEnvironmentWrite::Entry::Strings;
    write.names = { "APPBOX_ENV_HOST" };
    write.values = { "changed" };
    write.read = { "APPBOX_ENV_HOST" };

    const auto written = ProbeEnvironmentWrite.Call(write, GetCWD(), config).get<ProtocolEnvironmentWrite::Rsp>();
    ASSERT_EQ(written.stored.size(), 1u);
    EXPECT_TRUE(written.stored[0]);
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
