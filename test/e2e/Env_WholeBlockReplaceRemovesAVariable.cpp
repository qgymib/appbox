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
 * 1. The isolation file lists `APPBOX_ENV_LAYER` with the value `layer`; the
 *    host holds neither of the two variables of this case.
 * 2. A first run replaces the whole environment with a block which carries
 *    `APPBOX_ENV_BLOCK=block` and which does not carry `APPBOX_ENV_LAYER`.
 * 3. A second run reads both variables.
 *
 * Expected:
 * 1. The second run sees `APPBOX_ENV_BLOCK=block`: the variable the block added
 *    was kept.
 * 2. `APPBOX_ENV_LAYER` is not part of the environment: the block removed it,
 *    and the removal was kept, so the row of the archive does not bring the
 *    variable back. The state records the difference between the block and the
 *    environment of the sandbox and not the block itself, which is what keeps a
 *    replacement from freezing the environment of the host into the state.
 * 3. The host holds neither variable.
 */
TEST_F(E2E_Env, WholeBlockReplaceRemovesAVariable)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    ASSERT_TRUE(WriteEnvironmentIsolationFile(
        GetCWD(), {
                      { L"APPBOX_ENV_LAYER", L"layer", appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Replace, L"" }
    }));

    ProtocolEnvironmentWrite::Req write;
    write.entry = ProtocolEnvironmentWrite::Entry::Strings;
    write.names = { "APPBOX_ENV_BLOCK" };
    write.values = { "block" };
    write.remove = { "APPBOX_ENV_LAYER" };
    write.read = { "APPBOX_ENV_BLOCK", "APPBOX_ENV_LAYER" };

    const auto written = ProbeEnvironmentWrite.Call(write, GetCWD(), config).get<ProtocolEnvironmentWrite::Rsp>();
    ASSERT_EQ(written.stored.size(), 1u);
    EXPECT_TRUE(written.stored[0]);
    ASSERT_EQ(written.values.size(), 2u);
    EXPECT_TRUE(written.found[0]);
    EXPECT_EQ(written.values[0], "block");
    EXPECT_FALSE(written.found[1]);

    ProtocolEnvironmentRead::Req read;
    read.names = { "APPBOX_ENV_BLOCK", "APPBOX_ENV_LAYER" };

    const auto rsp = ProbeEnvironmentRead.Call(read, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 2u);
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "block");
    EXPECT_FALSE(rsp.found[1]);

    ASSERT_TRUE(tree.Verify());
}
