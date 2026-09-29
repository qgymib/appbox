#include "probe/EnvironmentRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "SandboxLayout.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds both variables of the case.
 * 2. `00-foo.zip` prepends `v0` to the first variable, so the layer below the
 *    last package composes `v0;host`.
 * 3. `01-bar.zip` isolates both variables as `Full` with the value `v1` and
 *    `v1h`.
 * 4. The sandboxed application reads both variables.
 *
 * Expected:
 * 1. The first variable reports `v1` alone, so `Full` drops the value of the
 *    layer below it as well as the value of the host.
 * 2. The second variable reports `v1h` alone, so `Full` hides the value of the
 *    host even while the layers below name no value of their own.
 */
TEST_F(E2E_Patch, EnvironmentFullDropsTheLayersBelow)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const HostEnvironmentVariable host_of_the_chain(L"APPBOX_PATCH_FULL", L"host");
    const HostEnvironmentVariable host_of_the_host(L"APPBOX_PATCH_HIDDEN", L"hx");

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"00-foo.zip", {}, {}, {}, PatchNetwork{},
                          PatchEnvironment{ { { L"APPBOX_PATCH_FULL", L"v0", appbox::EnvironmentIsolation::WriteCopy,
                                                appbox::EnvironmentMergeMode::Prepend, L";" } } }));
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"01-bar.zip",
                          {
    },
                          {}, {}, PatchNetwork{},
                          PatchEnvironment{ { { L"APPBOX_PATCH_FULL", L"v1", appbox::EnvironmentIsolation::Full,
                                                appbox::EnvironmentMergeMode::Replace, L"" },
                                              { L"APPBOX_PATCH_HIDDEN", L"v1h", appbox::EnvironmentIsolation::Full,
                                                appbox::EnvironmentMergeMode::Replace, L"" } } }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_PATCH_FULL", "APPBOX_PATCH_HIDDEN" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 2u);
    ASSERT_EQ(rsp.found.size(), 2u);

    /* The mode drops the value the layer below composed. */
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "v1");

    /* The mode drops the value of the host as well. */
    EXPECT_TRUE(rsp.found[1]);
    EXPECT_EQ(rsp.values[1], "v1h");

    ASSERT_TRUE(tree.Verify());
}
