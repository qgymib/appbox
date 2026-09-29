#include "probe/EnvironmentRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include <algorithm>
#include <string>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds `APPBOX_ENV_HOST=foo`.
 * 2. The isolation file lists the variable with `Write Copy`, the merge mode
 *    `Prepend` and the merge string `;` and the value `bar`.
 * 3. The sandboxed application reads the variable, enumerates its environment
 *    and expands a reference to the variable.
 *
 * Expected:
 * 1. The application sees `bar;foo`.
 * 2. The block which enumerates the environment reports the same value.
 * 3. The expansion of `%APPBOX_ENV_HOST%` reports the same value.
 */
TEST_F(E2E_Env, WriteCopy_Prepend)
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
                                                   appbox::EnvironmentMergeMode::Prepend, L";" }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_HOST" };
    req.expand = { "%APPBOX_ENV_HOST%" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 1u);
    ASSERT_EQ(rsp.expanded.size(), 1u);
    ASSERT_EQ(rsp.ansi_expanded.size(), 1u);

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "bar;foo");
    EXPECT_EQ(rsp.ansi_values[0], "bar;foo");

    EXPECT_EQ(rsp.expanded[0], "bar;foo");
    EXPECT_EQ(rsp.ansi_expanded[0], "bar;foo");

    EXPECT_NE(std::find(rsp.entries.begin(), rsp.entries.end(), "APPBOX_ENV_HOST=bar;foo"), rsp.entries.end());
    EXPECT_NE(std::find(rsp.ansi_entries.begin(), rsp.ansi_entries.end(), "APPBOX_ENV_HOST=bar;foo"),
              rsp.ansi_entries.end());

    ASSERT_TRUE(tree.Verify());
}
