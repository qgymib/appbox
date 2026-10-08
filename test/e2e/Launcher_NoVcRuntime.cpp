#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ModuleList.hpp"
#include "utils/ProbeCall.hpp"
#include "probe/LoadedModules.hpp"
#include "SandboxLayout.hpp"
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Launcher_NoVcRuntime;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the launcher configuration of a case.
 *
 * The configuration carries the filesystem of the sandbox the launcher mounts:
 * an empty upper layer and a lower layer below `filesystem`, which is the
 * layout a packaged archive uses. The builder writes the sandbox injection
 * modules of this build into the resource root of the case, so the launcher
 * injects them into the probe process.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @return The configuration without any startup file.
 */
appbox::LauncherConfig BuildConfig(const std::filesystem::path& root)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"filesystem\\#USERPROFILE#", {}) })
    });
    /* clang-format on */

    return tree.Build();
}

} // namespace

/**
 * Condition:
 * 1. The launcher starts the probe process inside the sandbox, so the probe
 *    runs with the injection modules of the resource root loaded.
 *
 * Expected:
 * 1. The module list of the sandboxed process carries the 64 bit injection
 *    module, which proves that the probe reported from inside the sandbox.
 * 2. The module list carries no module the Visual C++ Redistributable installs:
 *    the probe, the injection module and every module they depend on are self
 *    contained, so a machine which runs the sandbox needs no redistributable.
 *    The runtime of the operating system (`ucrtbase.dll`, `msvcrt.dll` and
 *    `msvcp_win.dll`) may appear in the list, because the modules of Windows
 *    which the process loads import it themselves; a product of this repository
 *    imports none of them, which the import tables of
 *    `Unit_StaticRuntime.ProductsImportNoVcRuntime` pin.
 */
TEST_F(E2E_Launcher_NoVcRuntime, TheSandboxedProcessLoadsNoVcRuntime)
{
    const auto config = BuildConfig(GetCWD());

    const auto rsp =
        ProbeLoadedModules.Call(nlohmann::json::object(), GetCWD(), config).get<ProtocolLoadedModules::Rsp>();

    ASSERT_FALSE(rsp.modules.empty());
    EXPECT_TRUE(HasModule(rsp.modules, appbox::layout::kSandbox64DllName)) << "the probe reported from the host";

    for (const auto& module : rsp.modules)
    {
        EXPECT_FALSE(IsVcRedistributableModule(module)) << "the sandboxed process loads " << module;
    }
}
