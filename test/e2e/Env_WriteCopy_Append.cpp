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
 * 2. The isolation file lists the variable with `Write Copy`, the merge mode
 *    `Append` and the merge string `;` and the value `bar`.
 * 3. The sandboxed application reads the variable.
 *
 * Expected:
 * 1. The application sees `foo;bar`: the stored value is joined behind the
 *    value of the host.
 */
TEST_F(E2E_Env, WriteCopy_Append)
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
                                                   appbox::EnvironmentMergeMode::Append, L";" }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_HOST" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 1u);

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "foo;bar");

    ASSERT_TRUE(tree.Verify());
}
