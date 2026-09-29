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
 *    `Replace` and the value `bar`.
 * 3. The sandboxed application reads the variable.
 *
 * Expected:
 * 1. The application sees `bar`: the merge mode replaces the value of the host.
 * 2. The ANSI entry point reports the same value.
 */
TEST_F(E2E_Env, WriteCopy_Replace)
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

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_HOST" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 1u);
    ASSERT_EQ(rsp.ansi_values.size(), 1u);

    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "bar");
    EXPECT_EQ(rsp.ansi_values[0], "bar");

    /*
     * The lowest reader of the process environment refuses to answer into a
     * buffer which the caller does not have: the loader of the operating system
     * asks for the search path of a DLL that way and copies the value into a
     * buffer it allocated afterwards, so an answer of success would make it copy
     * from the null pointer.
     */
    EXPECT_EQ(rsp.rtl_size_query_status, 0xC0000023u); /* STATUS_BUFFER_TOO_SMALL */
    EXPECT_NE(rsp.rtl_size_query_length, 0u);

    ASSERT_TRUE(tree.Verify());
}
