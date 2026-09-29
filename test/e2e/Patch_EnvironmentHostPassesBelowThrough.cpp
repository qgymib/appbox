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
 * 1. The host holds the first variable of the case and does not hold the
 *    second one.
 * 2. `00-foo.zip` replaces the first variable with `v0`, so the layer below the
 *    last package composes `v0`.
 * 3. `01-bar.zip` lists both variables with the merge mode `Host` and a value
 *    of its own.
 * 4. The sandboxed application reads both variables.
 *
 * Expected:
 * 1. The first variable reports `v0`, so `Host` passes the value of the layers
 *    below the layer through and ignores the value of the layer.
 * 2. The second variable is not part of the environment at all, so `Host`
 *    without a value below the layer and without a value of the host reports
 *    nothing.
 */
TEST_F(E2E_Patch, EnvironmentHostPassesBelowThrough)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const HostEnvironmentVariable host(L"APPBOX_PATCH_PASS", L"host");

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"00-foo.zip", {}, {}, {}, PatchNetwork{},
                          PatchEnvironment{ { { L"APPBOX_PATCH_PASS", L"v0", appbox::EnvironmentIsolation::WriteCopy,
                                                appbox::EnvironmentMergeMode::Replace, L"" } } }));
    ASSERT_TRUE(WritePatchPackage(
        patch_dir / L"01-bar.zip",
        {
    },
        {}, {}, PatchNetwork{},
        PatchEnvironment{ { { L"APPBOX_PATCH_PASS", L"ignored", appbox::EnvironmentIsolation::WriteCopy,
                              appbox::EnvironmentMergeMode::Host, L"" },
                            { L"APPBOX_PATCH_ABSENT", L"ignored", appbox::EnvironmentIsolation::WriteCopy,
                              appbox::EnvironmentMergeMode::Host, L"" } } }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_PATCH_PASS", "APPBOX_PATCH_ABSENT" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 2u);
    ASSERT_EQ(rsp.found.size(), 2u);

    /* The mode passes the value below the layer through. */
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "v0");

    /* The mode reports nothing while no layer holds a value. */
    EXPECT_FALSE(rsp.found[1]);
    EXPECT_EQ(rsp.values[1], "");

    ASSERT_TRUE(tree.Verify());
}
