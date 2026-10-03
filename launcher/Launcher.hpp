#ifndef APPBOX_LAUNCHER_HPP
#define APPBOX_LAUNCHER_HPP

#include <memory>
#include <string>
#include <vector>
#include "sandbox/Config.hpp"
#include "RemoteServer.hpp"
#include "utils/SandboxPaths.hpp"
#include "Config.hpp"

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
     */
    explicit AppBoxLauncherRuntime(const std::string& log_level);
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
};

/**
 * @brief Get the state of the current run.
 * @return The state.
 */
AppBoxLauncher& LauncherApp();

#endif // APPBOX_LAUNCHER_HPP
