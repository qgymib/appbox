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
 * 1. The host holds neither variable of the case.
 * 2. `00-foo.zip` configures the first variable.
 * 3. `01-bar.zip` carries an environment document which is not valid JSON, so
 *    the layer cannot be applied.
 * 4. `02-baz.zip` configures the second variable.
 * 5. The sandboxed application reads both variables.
 *
 * Expected:
 * 1. The first variable reports the value of `00-foo.zip`, so a broken package
 *    does not fail the run and does not drop the layers below it.
 * 2. The second variable reports the value of `02-baz.zip`, so the layers above
 *    a broken package are applied like they are without it.
 */
TEST_F(E2E_Patch, BrokenEnvironmentResourcesAreSkipped)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"environment", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"00-foo.zip", {}, {}, {}, PatchNetwork{},
                          PatchEnvironment{ { { L"APPBOX_PATCH_KEEP", L"v0", appbox::EnvironmentIsolation::WriteCopy,
                                                appbox::EnvironmentMergeMode::Replace, L"" } } }));
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"01-bar.zip", {}, {}, {}, PatchNetwork{},
                                  PatchEnvironment{ {}, "{\"version\":1,\"entries\":[" }));
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"02-baz.zip", {}, {}, {}, PatchNetwork{},
                          PatchEnvironment{ { { L"APPBOX_PATCH_LATER", L"v2", appbox::EnvironmentIsolation::WriteCopy,
                                                appbox::EnvironmentMergeMode::Replace, L"" } } }));

    ProtocolEnvironmentRead::Req req;
    req.names = { "APPBOX_PATCH_KEEP", "APPBOX_PATCH_LATER" };

    const auto rsp = ProbeEnvironmentRead.Call(req, GetCWD(), config).get<ProtocolEnvironmentRead::Rsp>();
    ASSERT_EQ(rsp.values.size(), 2u);
    ASSERT_EQ(rsp.found.size(), 2u);

    /* The layers below the broken package stay in place. */
    EXPECT_TRUE(rsp.found[0]);
    EXPECT_EQ(rsp.values[0], "v0");

    /* The layer above the broken package is applied. */
    EXPECT_TRUE(rsp.found[1]);
    EXPECT_EQ(rsp.values[1], "v2");

    ASSERT_TRUE(tree.Verify());
}
