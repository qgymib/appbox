#ifndef APPBOX_LAUNCHER_HPP
#define APPBOX_LAUNCHER_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "sandbox/Config.hpp"
#include "RemoteAccepter.hpp"
#include "msg/RunState.hpp"
#include "utils/SandboxModules.hpp"
#include "utils/SandboxPaths.hpp"
#include "utils/StartupLayout.hpp"
#include "Config.hpp"

namespace appbox
{
class SplashScreen;
class ControlPlane;
} // namespace appbox

struct AppBoxLauncherRuntime
{
    typedef std::shared_ptr<AppBoxLauncherRuntime> Ptr;

    /**
     * @brief Build the runtime of one run.
     *
     * The runtime prepares the sandbox of the run: it loads the patch packages,
     * mounts the layers, rebuilds the registry image and fills the injection
     * data of the processes the run starts. The control pipe of the sandbox is
     * served by the control layer of the run, which exists before the runtime
     * does, so the runtime neither creates nor starts a pipe server.
     *
     * @param[in] log_level Level the run reports: `trace`, `debug`, `info`,
     *                      `warn`, `err`, `critical` or `off`. It is handed to
     *                      the sandbox of every process the run starts, so the
     *                      processes of a run report the same levels.
     * @param[in] log_dir Directory the sandbox writes the log file of every
     *                    process of the run into, in UTF-8. It is empty when
     *                    the run asks for no log file, which leaves every
     *                    process of the run without one.
     * @param[in] pipe_path Name of the control pipe of the sandbox, in UTF-8.
     *                      The sandbox of every process of the run connects to
     *                      it to reach the launcher.
     */
    AppBoxLauncherRuntime(const std::string& log_level, const std::wstring& log_dir, const std::string& pipe_path);
    ~AppBoxLauncherRuntime();

    appbox::SandboxConfig inject_data; /* Inject data information */
};

/**
 * @brief Facts a run reports about itself through the state query.
 *
 * The facts are written by the thread which prepares the run and read by the
 * RPC thread which answers a query, so the members a reader polls while the run
 * still works are atomics and the texts are guarded by the mutex. A fact which
 * describes a stage of the past - the screen of the session was created and
 * closed again - is kept instead of being cleared, because it is what a caller
 * observes once the run passed that stage.
 */
struct LauncherRunFacts
{
    /** Last stage the run reported, as `appbox::StartupStage`. */
    std::atomic<int> stage{ static_cast<int>(appbox::StartupStage::Preparing) };

    /** Whether the runtime of the sandbox exists, which the requests need. */
    std::atomic<bool> prepared{ false };

    /** Whether a startup screen was created for the session. */
    std::atomic<bool> screen_shown{ false };

    /** Whether the startup screen of the run was closed again. */
    std::atomic<bool> screen_closed{ false };

    /** Width of the startup image the screen decoded, zero while none was decoded. */
    std::atomic<int> image_width{ 0 };

    /** Height of the startup image the screen decoded, zero while none was decoded. */
    std::atomic<int> image_height{ 0 };

    mutable std::mutex mutex; ///< Guards the two texts below.

    std::string image_error;     ///< Reason the startup image could not be decoded, empty on success.
    std::string extraction_root; ///< Folder a self-extracting run extracted its payload into, in UTF-8.
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
    appbox::SandboxModulePaths                  sandbox_modules; /* Released injection modules */
    AppBoxLauncherRuntime::Ptr                  runtime;         /* Runtime information */
    std::shared_ptr<appbox::ControlPlane>       control;         /* Control layer of the master instance */
    std::vector<const appbox::LauncherStartup*> startups;        /* Startup files to run */
    std::vector<std::string>  extra_args;    /* Arguments of the command line the programs of the run receive */
    std::string               startup_error; /* Error which refuses the run */
    bool                      shell = false; /* True to run the shell of the host in the sandbox */
    std::wstring              shell_path;    /* Shell of the host to run, empty outside the shell mode */
    std::vector<std::wstring> shell_args;    /* Command the shell runs, empty for an interactive shell */

    /**
     * @brief Startup screen of the run, null while the session shows none.
     *
     * The screen belongs to the run and not to the state: the launcher creates
     * it before the sandbox is prepared and destroys it as soon as the
     * processes of the run were started, so the pointer is valid while the run
     * works and null afterwards.
     */
    appbox::SplashScreen* splash = nullptr;

    LauncherRunFacts run_facts; /* Facts the state query of the run reports */

    /**
     * @brief Record the startup screen the run created.
     *
     * The call keeps what the screen decodes and whether it exists at all, so
     * the state query of the run reports the screen of the session without a
     * reader having to look at the window: a screen which the session turned
     * off is a run without a screen, and a screen which was created is a fact
     * which stays after the screen was closed again.
     *
     * @param[in] screen The screen of the run, which the run owns.
     */
    void RecordStartupScreen(const appbox::SplashScreen& screen);

    /**
     * @brief Record that the startup screen of the run was closed.
     *
     * The call is harmless for a run which never created a screen, so every
     * path which leaves the run may call it.
     */
    void RecordStartupScreenClosed();

    /**
     * @brief Record the folder a self-extracting run extracted its payload into.
     * @param[in] root Host folder of the extraction, in UTF-8; empty for a run
     *                 which carries no payload.
     */
    void RecordExtractionRoot(const std::string& root);

    /**
     * @brief Fill the run facts of a state answer.
     *
     * The call reports what the run recorded about itself: the stage it reached,
     * whether the sandbox is prepared, the startup screen of the session and the
     * folder of a self-extracting payload. The programs of the run and the
     * counters of the requests belong to the control layer, which fills them
     * (see `appbox::ControlPlane::FillRunState()`).
     *
     * @param[out] state Answer to fill, whose control layer members are left
     *                   untouched.
     */
    void FillRunState(appbox::MsgRunState::Rsp& state) const;
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
