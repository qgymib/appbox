#include "probe/EnvironmentRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds neither `APPBOX_ENV_JOINED` nor `APPBOX_ENV_HOSTED`.
 * 2. The isolation file lists `APPBOX_ENV_JOINED` with the merge mode `Prepend`
 *    and the merge string `;`, and `APPBOX_ENV_HOSTED` with the merge mode
 *    `Host`.
 * 3. The sandboxed application reads both variables.
 *
 * Expected:
 * 1. The application sees `bar` for `APPBOX_ENV_JOINED`: the merge string joins
 *    two values and is not written while there is only one.
 * 2. The application does not see `APPBOX_ENV_HOSTED` at all: the merge mode
 *    ignores the stored value and the host holds none either.
 */
TEST_F(E2E_Env, MissingHostValue_KeepsTheConfiguredValue)
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
                      { L"APPBOX_ENV_JOINED", L"bar", appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Prepend, L";" },
                      { L"APPBOX_ENV_HOSTED", L"bar", appbox::EnvironmentIsolation::WriteCopy,
                       appbox::EnvironmentMergeMode::Host,    L""  }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_JOINED", "APPBOX_ENV_HOSTED" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 2u);
    ASSERT_EQ(rsp.found.size(), 2u);

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "bar");

    EXPECT_FALSE(rsp.found[1]);
    EXPECT_EQ(rsp.values[1], "");

    ASSERT_TRUE(tree.Verify());
}
