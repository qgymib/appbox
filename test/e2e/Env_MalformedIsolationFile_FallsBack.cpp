#include "probe/EnvironmentRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds `APPBOX_ENV_HOST=foo`.
 * 2. The isolation file of the archive is malformed: an entry carries no value,
 *    no isolation mode and no merge mode.
 * 3. The sandboxed application reads the variable.
 *
 * Expected:
 * 1. The application sees `foo`: the malformed document is ignored as a whole
 *    instead of being applied in part.
 * 2. The sandbox still runs.
 */
TEST_F(E2E_Env, MalformedIsolationFile_FallsBack)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const HostEnvironmentVariable host(L"APPBOX_ENV_HOST", L"foo");

    ASSERT_TRUE(WriteEnvironmentIsolationFileText(
        GetCWD(), "{ \"version\": 1, \"entries\": [ { \"name\": \"APPBOX_ENV_HOST\" } ] }"));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_HOST" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 1u);

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "foo");

    ASSERT_TRUE(tree.Verify());
}
