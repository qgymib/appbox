#include "probe/EnvironmentRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "SandboxLayout.hpp"
#include <algorithm>
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

/**
 * Condition:
 * 1. The host holds `APPBOX_PATCH_ENV=vx` and holds no `APPBOX_PATCH_ONLY`.
 * 2. `00-foo.zip` prepends `v0` to the variable and replaces a variable of its
 *    own.
 * 3. `01-bar.zip` appends `v1` to the variable, and names neither the host
 *    value nor the variable of the first package.
 * 4. The sandboxed application reads both variables.
 *
 * Expected:
 * 1. The variable reports `v0;vx;v1`, so every layer applies its own merge mode
 *    to the value the layers below it composed.
 * 2. The variable only the first package names reports `v0only`, so a layer
 *    which does not name a variable keeps the value of the layers below it.
 */
TEST_F(E2E_Patch, EnvironmentLayersComposeInOrder)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const HostEnvironmentVariable host(L"APPBOX_PATCH_ENV", L"vx");

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(WritePatchPackage(
        patch_dir / L"00-foo.zip",
        {
    },
        {}, {}, PatchNetwork{},
        PatchEnvironment{ { { L"APPBOX_PATCH_ENV", L"v0", appbox::EnvironmentIsolation::WriteCopy,
                              appbox::EnvironmentMergeMode::Prepend, L";" },
                            { L"APPBOX_PATCH_ONLY", L"v0only", appbox::EnvironmentIsolation::WriteCopy,
                              appbox::EnvironmentMergeMode::Replace, L"" } } }));
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"01-bar.zip", {}, {}, {}, PatchNetwork{},
                          PatchEnvironment{ { { L"APPBOX_PATCH_ENV", L"v1", appbox::EnvironmentIsolation::WriteCopy,
                                                appbox::EnvironmentMergeMode::Append, L";" } } }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_PATCH_ENV", "APPBOX_PATCH_ONLY" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 2u);
    ASSERT_EQ(rsp.found.size(), 2u);

    /* The chain composes from the value of the host outwards. */
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "v0;vx;v1");

    /* A variable no later layer names keeps the value below it. */
    EXPECT_TRUE(rsp.found[1]);
    EXPECT_EQ(rsp.values[1], "v0only");

    /* The block which enumerates the environment reports the same values. */
    EXPECT_NE(std::find(rsp.entries.begin(), rsp.entries.end(), "APPBOX_PATCH_ENV=v0;vx;v1"), rsp.entries.end());

    ASSERT_TRUE(tree.Verify());
}
