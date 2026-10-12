#ifndef APPBOX_LAUNCHER_HPP
#define APPBOX_LAUNCHER_HPP

#include <memory>
#include <string>
#include <vector>
#include "sandbox/Config.hpp"
#include "RemoteServer.hpp"
#include "utils/SandboxPaths.hpp"
#include "utils/StartupLayout.hpp"
#include "Config.hpp"

namespace appbox
{
class SplashScreen;
}

struct AppBoxLauncherRuntime
{
    typedef std::shared_ptr<AppBoxLauncherRuntime> Ptr;

    /**
     * @brief Build the runtime of one run.
     *
     * @param[in] log_level Level the run reports: `trace`, `debug`, `info`,
     *                      `warn`, `err`, `critical` or `off`. It is handed to
     *                      the sandbox of every process the run starts, so the
     *                      processes of a run report the same levels.
     * @param[in] log_dir Directory the sandbox writes the log file of every
     *                    process of the run into, in UTF-8. It is empty when
     *                    the run asks for no log file, which leaves every
     *                    process of the run without one.
     */
    AppBoxLauncherRuntime(const std::string& log_level, const std::wstring& log_dir);
    ~AppBoxLauncherRuntime();

    appbox::SandboxConfig     inject_data; /* Inject data information */
    appbox::RemoteServer::Ptr pipe_server; /* Pipe server for remote communication */
};

/**
 * @brief State of one run of the launcher.
 *
 * The launcher has no user interface, so the state of a run is a plain global
 * instead of the application object of a window toolkit.
 */
struct AppBoxLauncher
{
    appbox::LauncherConfig                      launcher_config; /* Launcher configuration */
    appbox::SandboxPaths                        sandbox_paths;   /* Layout of the sandbox */
    AppBoxLauncherRuntime::Ptr                  runtime;         /* Runtime information */
    std::vector<const appbox::LauncherStartup*> startups;        /* Startup files to run */
    std::string                                 startup_error;   /* Error which refuses the run */
    bool                                        shell = false;   /* True to run the shell of the host in the sandbox */
    std::wstring                                shell_path; /* Shell of the host to run, empty outside the shell mode */
    std::vector<std::wstring>                   shell_args; /* Command the shell runs, empty for an interactive shell */

    /**
     * @brief Startup screen of the run, null while the session shows none.
     *
     * The screen belongs to the run and not to the state: the launcher creates
     * it before the sandbox is prepared and destroys it as soon as the
     * processes of the run were started, so the pointer is valid while the run
     * works and null afterwards.
     */
    appbox::SplashScreen* splash = nullptr;
};

/**
 * @brief Get the state of the current run.
 * @return The state.
 */
AppBoxLauncher& LauncherApp();

/**
 * @brief Report the stage of the run to the startup screen.
 *
 * The call is harmless while the run shows no screen, so every part of the
 * launcher reports its progress without asking whether a screen exists.
 *
 * @param[in] stage Stage the run is in.
 * @param[in] fraction Progress inside the stage, `0` at its beginning and `1`
 *                     at its end.
 */
void ReportStartupStage(appbox::StartupStage stage, double fraction);

#endif // APPBOX_LAUNCHER_HPP
