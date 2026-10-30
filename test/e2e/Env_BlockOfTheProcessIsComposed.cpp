#include "probe/EnvironmentRead.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Env;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds `APPBOX_ENV_BLOCK=host`.
 * 2. The isolation file lists `APPBOX_ENV_BLOCK` with the isolation mode `Full`
 *    and the value `sandbox`.
 * 3. The probe reads the environment block the process parameters point at,
 *    which is the block the process itself carries and not an answer of an
 *    entry point of the environment API.
 *
 * Expected:
 * 1. The block of the process holds `sandbox`, so the composed environment is
 *    the environment of the process and not a view of an entry point.
 * 2. The block does not hold the value of the host.
 * 3. The entry points report the same value as the block.
 */
TEST_F(E2E_Env, BlockOfTheProcessIsComposed)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const HostEnvironmentVariable host(L"APPBOX_ENV_BLOCK", L"host");

    ASSERT_TRUE(WriteEnvironmentIsolationFile(GetCWD(),
                                              {
                                                  { L"APPBOX_ENV_BLOCK", L"sandbox", appbox::EnvironmentIsolation::Full,
                                                   appbox::EnvironmentMergeMode::Replace, L"" }
    }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_ENV_BLOCK" };

    const auto rsp = Probe(GetCWD(), config).Call<ProtocolEnvironmentRead>(req);
    ASSERT_EQ(rsp.block_values.size(), 1u);
    ASSERT_EQ(rsp.block_found.size(), 1u);

    EXPECT_TRUE(rsp.block_found[0]);
    EXPECT_EQ(rsp.block_values[0], "sandbox");

    for (const auto& entry : rsp.block_entries)
    {
        EXPECT_EQ(entry.find("APPBOX_ENV_BLOCK=host"), std::string::npos);
    }

    ASSERT_EQ(rsp.values.size(), 1u);
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "sandbox");

    ASSERT_TRUE(tree.Verify());
}
