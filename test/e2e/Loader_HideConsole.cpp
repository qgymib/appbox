#include "probe/ConsoleWindow.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "loader/Config.hpp"
#include <filesystem>

typedef appbox::test::CommonFixture E2E_Loader_HideConsole;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the loader configuration of a case.
 *
 * The configuration carries the layout of a packed archive: the state of the
 * sandbox below `data` and the read-only resources below `app`.
 *
 * @param[in] root Root directory of the filesystem of the case.
 * @return The configuration without any startup file.
 */
appbox::LoaderConfig BuildConfig(const std::filesystem::path& root)
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
 * 1. The configuration of the case starts the probe once, like the single main
 *    program of a packaged application.
 * 2. The loader starts the probe through the real chain of the sandbox, so the
 *    probe is a console program started by a program without a console.
 *
 * Expected:
 * 1. The probe process still owns a console, so its standard streams keep
 *    working.
 * 2. Its console window is not visible: the loader starts a console program
 *    without a console window, so nothing pops up on the desktop of the machine
 *    the cases run on.
 */
TEST_F(E2E_Loader_HideConsole, ProbeConsoleWindowIsHidden)
{
    const auto config = BuildConfig(GetCWD());

    const auto rsp =
        ProbeConsoleWindow.Call(nlohmann::json::object(), GetCWD(), config).get<ProtocolConsoleWindow::Rsp>();

    EXPECT_TRUE(rsp.attached);
    EXPECT_FALSE(rsp.visible);
}
