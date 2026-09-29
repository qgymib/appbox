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
 * 2. The isolation file lists the variable with `Write Copy` and the merge mode
 *    `Host` and the value `bar`.
 * 3. The sandboxed application reads the variable.
 *
 * Expected:
 * 1. The application sees `foo`: the merge mode uses the value of the host and
 *    ignores the value of the row.
 */
TEST_F(E2E_Env, WriteCopy_Host)
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
                                                   appbox::EnvironmentMergeMode::Host, L"" }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_HOST" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 1u);

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "foo");

    ASSERT_TRUE(tree.Verify());
}
