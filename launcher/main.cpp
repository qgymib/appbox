/*
 * <winsock2.h> has to precede <windows.h>, which the headers below pull in
 * (<Shlobj.h>, the project headers): it defines _WINSOCKAPI_, so <windows.h>
 * skips the winsock 1.1 header, which cannot be included next to the winsock 2
 * header the RPC server reaches through asio.
 */
#include <winsock2.h>
#include <CLI/CLI.hpp>
#include <detours.h>
#include <spdlog/spdlog.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <base64.hpp>
#include "EmbeddedResource.hpp"
#include "SandboxId.hpp"
#include "sandbox/utils/Defines.hpp"
#include "utils/CommandLineOptions.hpp"
#include "utils/GetExecutableDir.hpp"
#include "utils/ProcessJobSet.hpp"
#include "utils/SandboxModules.hpp"
#include "utils/SelfExtraction.hpp"
#include "utils/Shell.hpp"
#include "utils/VersionInfo.hpp"
#include "utils/ViewPathResolver.hpp"
#include "utils/WinCall.hpp"
#include "EmbeddedResourceIds.h"
#include "control/ControlPlane.hpp"
#include "rpc/__init__.hpp"
#include "Launcher.hpp"
#include "Splash.hpp"
#include "WString.hpp"

/**
 * @brief Resolve the shell of the host which the sandbox runs.
 *
 * The shell is the `cmd.exe` of the machine which runs the sandbox, resolved
 * outside of the isolation; the command it runs is the remaining command line
 * of the launcher, which `BuildShellArguments()` turns into the arguments of the
 * shell.
 *
 * @param[in] params Command of the shell, in UTF-8; empty to run an
 *                   interactive shell.
 * @return true when the shell was resolved, false when the machine offers no
 *         shell to run.
 */
static bool PrepareShell(const std::vector<std::string>& params)
{
    LauncherApp().shell = true;

    LauncherApp().shell_path = appbox::ResolveShellPath();
    if (LauncherApp().shell_path.empty())
    {
        return false;
    }

    LauncherApp().shell_args = appbox::BuildShellArguments(params);
    SPDLOG_INFO("Run the shell '{}' of the host in the sandbox", appbox::WideToUTF8(LauncherApp().shell_path));
    return true;
}

/**
 * @brief Select the startup files the launcher runs.
 *
 * The selection happens on the main thread, so a rejected trigger is reported
 * before the working thread starts.
 *
 * A run of the shell selects no startup file at all: the sandbox of the run is
 * described by the configuration, while the shell replaces the application the
 * configuration starts. A configuration without a startup file is therefore
 * not an error for such a run.
 *
 * @param[in] opt The command line options of the launcher.
 */
static void SelectStartupsForRun(const appbox::CommandLineOptions& opt)
{
    if (opt.shell)
    {
        return;
    }

    appbox::LauncherStartSelection selection;
    selection.kind = opt.has_startup_trigger ? appbox::LauncherStartSelectionKind::Trigger
                                             : appbox::LauncherStartSelectionKind::Auto;
    selection.trigger = opt.startup_trigger;

    std::string error_code;
    std::string error_message;
    if (!appbox::SelectStartupFiles(LauncherApp().launcher_config, selection, LauncherApp().startups, error_code,
                                    error_message))
    {
        LauncherApp().startup_error = error_message;
    }
}

/**
 * @brief Build the processes the initial run of the launcher starts.
 *
 * A run of the shell starts the shell of the host alone, which makes the run
 * ignore the startup files of the configuration completely. The console window
 * of the shell is always shown, because the shell is meant to be used
 * interactively: the `hide_console` flag of the configuration keeps describing
 * the startup files only.
 *
 * Every other run starts the startup files which `SelectStartupsForRun()`
 * selected, in configuration order. A startup file is started the way its kind
 * asks for: a program on its own, a script through the interpreter of the host
 * (see `appbox::BuildStartupInvocation()`). A file whose interpreter the
 * machine does not offer is reported and refuses the run, because the sandbox
 * would otherwise start only a part of the application.
 *
 * The view of the run belongs to the control layer, which builds it once and
 * asks it for every program of the run: a request of another launcher resolves
 * its programs through the same view.
 *
 * @param[in] resolver View of the run.
 * @return The processes to start, in the order they are started in.
 */
static std::vector<appbox::LaunchTarget> BuildInitialTargets(const appbox::ViewPathResolver& resolver)
{
    std::vector<appbox::LaunchTarget> targets;

    if (LauncherApp().shell)
    {
        appbox::LaunchTarget target;
        std::string          error;
        appbox::BuildShellTarget(LauncherApp().shell_path, LauncherApp().shell_args, resolver, target, error);
        targets.push_back(std::move(target));
        return targets;
    }

    /*
     * The arguments of the command line of this run follow the arguments of
     * every startup file, which is what makes the remaining command line the
     * arguments of the sandboxed application.
     */
    appbox::BuildLaunchTargets(LauncherApp().startups, LauncherApp().extra_args,
                               LauncherApp().launcher_config.hide_console, resolver, targets);
    return targets;
}

/**
 * @brief Destroy the startup screen of the run.
 *
 * The screen is the only owner of its window, so releasing it closes the
 * screen. The state of the run stops referring to it first, because a report
 * which arrives afterwards would otherwise reach a destroyed screen.
 *
 * @param[in,out] splash Screen of the run, which is released.
 */
static void CloseStartupScreen(std::unique_ptr<appbox::SplashScreen>& splash)
{
    LauncherApp().splash = nullptr;
    splash.reset();

    /* The screen of the run is gone, which the state query of the run reports. */
    LauncherApp().RecordStartupScreenClosed();
}

/**
 * @brief Remove the extraction of a self-extracting program when the run ends.
 *
 * The run owns the folder, so every path which leaves RunLauncher() releases
 * it: the processes of the run were waited for before.
 */
class ExtractionCleanup
{
public:
    ~ExtractionCleanup()
    {
        appbox::RemoveExtraction(root_);
    }

    ExtractionCleanup() = default;
    ExtractionCleanup(const ExtractionCleanup&) = delete;
    ExtractionCleanup& operator=(const ExtractionCleanup&) = delete;

    /**
     * @brief Get the folder of the extraction.
     * @return The folder, empty while the program carries no payload.
     */
    std::wstring& Root()
    {
        return root_;
    }

private:
    std::wstring root_;
};

/**
 * @brief Run the processes the configuration describes and serve the sandbox.
 *
 * Every target is started before the first one is waited for, so the targets of
 * the run side by side instead of one after the other.
 *
 * The startup screen of the run covers the creation of the processes and is
 * destroyed as soon as every process was started, which is the moment the
 * packaged application is on its own.
 *
 * The processes of the run belong to the control layer, which holds them in one
 * set: the run starts its own programs into that set and waits for them with
 * the control loop of the layer, which also serves the requests of the other
 * launchers of the sandbox. A request wakes the wait up, so it is handled while
 * the programs of this run still work.
 *
 * Only the programs the configuration of the run describes decide the exit code:
 * a program which a request started is the work of another launcher and its
 * exit code is none of this run's.
 *
 * @param[in,out] splash Startup screen of the run, which is closed by the call.
 * @return The exit code of the run: the first non zero exit code of a target of
 *         the run, zero when every target exited with zero.
 */
static DWORD MainLauncher(std::unique_ptr<appbox::SplashScreen>& splash)
{
    auto       control = LauncherApp().control;
    const auto targets = BuildInitialTargets(control->Resolver());

    /*
     * Every target is started before the first one is waited for, so the
     * targets of the run side by side instead of one after the other. The
     * processes belong to the control layer, which waits for them and covers
     * the processes a request of another launcher adds later.
     */
    auto& jobs = control->Jobs();
    DWORD exit_code = 0;

    ReportStartupStage(appbox::StartupStage::StartingApplication, 0.0);

    for (const auto& target : targets)
    {
        if (!target.error.empty())
        {
            /*
             * The kind of the startup file could not be turned into a process,
             * which is a reason the run cannot work around: the interpreter it
             * needs is not on this machine, see BuildInitialTargets().
             */
            SPDLOG_ERROR("Failed to start '{}': {}", target.name, target.error);
            if (exit_code == 0)
            {
                exit_code = ERROR_FILE_NOT_FOUND;
            }
            continue;
        }

        /*
         * A target of the run inherits the environment and the working
         * directory of the launcher, which is what an empty option asks for.
         * The target is the work of this run, which is what makes its exit code
         * the one the run reports.
         */
        const auto ret = jobs.Start(target.image_path, target.exe_path, target.args, LauncherApp().runtime->inject_data,
                                    target.hide_console, appbox::ProcessJobOptions{}, appbox::JobOrigin::Initial);
        if (ret != 0)
        {
            SPDLOG_ERROR("Failed to start '{}': {}", target.name, ret);
            if (exit_code == 0)
            {
                exit_code = ret;
            }
            continue;
        }

        /* The screen follows the processes the run creates. */
        const auto created = static_cast<double>(jobs.Size());
        const auto total = targets.empty() ? 1.0 : static_cast<double>(targets.size());
        ReportStartupStage(appbox::StartupStage::StartingApplication, created / total);
    }

    /*
     * Every process the run starts was created, so the screen has done its
     * job: a failure of one target is reported by the exit code of the run and
     * not by a window which stays open.
     */
    CloseStartupScreen(splash);
    SPDLOG_INFO("the initial application of the sandbox was started");

    /*
     * The control loop of the layer serves the requests of the other launchers
     * and waits for the processes of the run, so a request which arrives while
     * the application works is handled at once.
     */
    const auto wait_result = control->WaitForPrograms();
    if (wait_result.status == appbox::WaitStatus::Error)
    {
        SPDLOG_ERROR("Failed to wait for the processes: {}", wait_result.error);
        if (exit_code == 0)
        {
            exit_code = wait_result.error;
        }
    }

    /*
     * Only the programs the configuration of the run describes decide the exit
     * code: a program of a request belongs to the launcher which asked for it,
     * and a request which was refused is no reason for this run to fail.
     */
    for (std::size_t i = 0; i < jobs.Size(); ++i)
    {
        auto* job = jobs.Job(i);
        if (job == nullptr)
        {
            continue;
        }

        const auto code = job->GetExitCode();
        SPDLOG_INFO("application exited with code {}", code);

        if (!jobs.IsInitial(i))
        {
            continue;
        }

        if (exit_code == 0)
        {
            exit_code = code;
        }
    }

    SPDLOG_INFO("the sandboxed application exited with code {}", exit_code);
    return exit_code;
}

/**
 * @brief Resolve the layout of the sandbox.
 *
 * The resources of the packed application and the state of the sandbox live in
 * the fixed directories of `common/SandboxLayout.hpp`, resolved against the
 * directory of the launcher program itself unless a configuration file was
 * given on the command line.
 *
 * @param[in] config_dir Directory which holds the configuration, empty to use
 *                       the directory of the launcher program.
 */
static void ResolveSandboxPaths(const std::wstring& config_dir)
{
    const auto dir = config_dir.empty() ? appbox::GetExecutableDir() : config_dir;
    LauncherApp().sandbox_paths = appbox::SandboxPaths::Resolve(dir);
}

/**
 * @brief Release the sandbox injection modules of the run.
 *
 * The modules are resources of the launcher executable which every run writes
 * into the cache directory of the sandbox (see `common/SandboxLayout.hpp`).
 * The release happens before the runtime is created, so a failure refuses the
 * run before anything starts instead of failing while the application is
 * already being started.
 *
 * An error which another preparation step already reported is kept: the run is
 * refused for that reason and the modules are not needed at all.
 *
 * @param[out] error Description of the failure, left untouched when the
 *                   modules were already prepared by another step.
 */
static void PrepareSandboxModules(std::string& error)
{
    if (!error.empty())
    {
        return;
    }

    appbox::ReleaseSandboxModules(LauncherApp().sandbox_paths.cache, LauncherApp().sandbox_modules, error);
}

/**
 * @brief Check the identifier of the sandbox of the run.
 *
 * The identifier is the identity of the packaged application and the only
 * source of the name of the control pipe, so a configuration which carries none
 * or carries a value which is no identifier refuses the run: a fallback name of
 * the run's own would silently open a second sandbox next to the one the archive
 * describes. The check runs before the sandbox state exists, so a refused run
 * leaves neither a `data` directory nor a pipe behind.
 *
 * @return The exit code of the run: zero when the identifier is usable,
 *         `ERROR_INVALID_DATA` otherwise.
 */
static DWORD CheckSandboxId()
{
    const auto& id = LauncherApp().launcher_config.sandbox_id;
    if (appbox::IsValidSandboxId(id))
    {
        return 0;
    }

    if (id.empty())
    {
        SPDLOG_ERROR("the launcher configuration carries no sandbox identifier, the run is refused");
    }
    else
    {
        SPDLOG_ERROR("the sandbox identifier '{}' of the launcher configuration is not valid, the run is refused", id);
    }

    return ERROR_INVALID_DATA;
}

static void LoadConfig()
{
    /*
     * The configuration is a resource of the launcher executable itself, which
     * the packer wrote into the payload: the extracted program carries its own
     * configuration, so no file beside it can go missing or describe a
     * different application. The resource is UTF-8 JSON without a byte order
     * mark (see `launcher/Config.hpp`).
     *
     * No fallback is tried: a launcher which was not packed carries no
     * configuration resource, and the run is refused instead of starting
     * nothing.
     */
    std::string_view payload;
    std::string      error;
    if (!appbox::ReadEmbeddedResource(nullptr, IDR_APPBOX_LAUNCHER_CONFIG, payload, error))
    {
        throw std::runtime_error("the launcher configuration is unavailable: " + error);
    }

    nlohmann::json j_cfg = nlohmann::json::parse(payload.begin(), payload.end());
    LauncherApp().launcher_config = j_cfg;
    ResolveSandboxPaths({});
}

/**
 * @brief Keep the arguments of the command line which belong to the programs of the run.
 *
 * The arguments which were not consumed by the launcher belong to the sandboxed
 * application, so every program of the run receives them. They are kept beside
 * the configuration and are not appended to it: the configuration is what a
 * request of another launcher builds its programs from, and a request must not
 * inherit the arguments of this run.
 *
 * @param[in] opt The command line options of the launcher.
 */
static void KeepRunArguments(const appbox::CommandLineOptions& opt)
{
    LauncherApp().extra_args = opt.extra_args;
}

/**
 * @brief Prepare the run of the shell of the host.
 *
 * The shell replaces the application of the configuration, so the remaining
 * arguments of the launcher are the command of the shell instead of the
 * arguments of a startup file, and the startup files of the configuration are
 * ignored by the run.
 *
 * @param[in] opt The command line options of the launcher.
 * @return true when the run may continue, false when the options contradict
 *         each other or the machine offers no shell to run.
 */
static bool PrepareShellRun(const appbox::CommandLineOptions& opt)
{
    if (!opt.shell)
    {
        KeepRunArguments(opt);
        return true;
    }

    if (opt.has_startup_trigger)
    {
        /* The two options name different programs to run. */
        LauncherApp().startup_error = "the options --X-AppBox-Shell and --X-AppBox-Startup cannot be used together";
        return false;
    }

    if (!PrepareShell(opt.extra_args))
    {
        LauncherApp().startup_error = "the shell of the host could not be resolved";
        return false;
    }

    return true;
}

/**
 * @brief Stop serving the sandbox and release the control layer of the run.
 *
 * The pipe of the run is stopped before the layer is released: the requests
 * which still wait are answered with the end of the run, and the answers which
 * are in flight are written before the pipe is closed. A launcher which asks for
 * a program of a sandbox which is ending is therefore told so instead of losing
 * its request to a pipe which disappeared.
 */
static void StopServing()
{
    auto control = LauncherApp().control;
    if (control)
    {
        control->Shutdown();
    }
}

/**
 * @brief Run this launcher as the master instance of its sandbox.
 *
 * The master instance prepares the sandbox, starts the programs of its own
 * configuration and serves the requests of the other launchers of the sandbox
 * until every process it started exited and no request waits any more.
 *
 * The control layer is built before the sandbox is prepared, so the requests of
 * the other launchers are accepted while this run extracts its payload and
 * mounts its layers. They are queued and handled once the sandbox exists, which
 * is what keeps a launcher of an archive which is being unpacked from being
 * lost.
 *
 * @param[in,out] opt The command line options of the run.
 * @param[in] accepter Accepter which won the race for the control pipe.
 * @param[in] pipe Name of the control pipe of the sandbox.
 * @return The exit code of the run.
 */
static DWORD RunMasterInstance(appbox::CommandLineOptions& opt, appbox::RemoteAccepter::Ptr accepter,
                               const std::string& pipe)
{
    /*
     * The startup screen of the run. It covers the preparation of the sandbox
     * and disappears as soon as the processes of the run were started, so it
     * belongs to the run and is released by every path which leaves it.
     *
     * The extraction of a self-extracting program is declared first, so the
     * screen is destroyed before the folder of the extraction is released.
     */
    ExtractionCleanup                     extraction;
    std::unique_ptr<appbox::SplashScreen> splash;

    try
    {
        /*
         * The control layer of the run owns the pipe server, the queue of the
         * requests of the other launchers and the view which resolves the
         * programs a request starts. It exists before the runtime, because the
         * pipe has to serve the requests of a second instance while this run
         * is still preparing its sandbox.
         */
        auto control = std::make_shared<appbox::ControlPlane>(std::move(accepter), LauncherApp().sandbox_paths.root);
        LauncherApp().control = control;

        /*
         * A pipe which serves no connection any more is the end of the run. The
         * callback runs on the IO thread of the server, which may neither block
         * nor stop the server, so it keeps the failure and wakes the control
         * loop up; the loop reports the error and stops the pipe itself. The
         * layer is captured weakly, because the server is owned by the layer and
         * a strong reference would keep both alive for ever.
         */
        std::weak_ptr<appbox::ControlPlane> weak_control = control;
        control->Server()->SetAcceptFailureCallback([weak_control](appbox::AcceptFailureKind kind, DWORD error_code) {
            if (auto layer = weak_control.lock())
            {
                layer->OnAcceptFailure(kind, error_code);
            }
        });

        /*
         * The methods of the launcher are registered per session, so the accepter
         * is told what to do with a connection before it starts: the callback
         * runs on the IO thread of the accepter, before the session of the
         * connection reads its first message.
         */
        control->Server()->SetSessionCallback(
            [](const appbox::RemoteSession::Ptr& session) { appbox::RpcInit(session); });

        if (!control->Server()->Start())
        {
            throw std::runtime_error("the control pipe of the sandbox cannot be served");
        }

        SPDLOG_DEBUG("pipe listen on {}", pipe);

        PrepareShellRun(opt);

        /*
         * A run without the injection modules cannot sandbox anything: the
         * modules are released into the cache directory of the sandbox, and a
         * failure is reported by the block below the startup selection, which
         * turns it into a non zero exit code and starts nothing.
         */
        PrepareSandboxModules(LauncherApp().startup_error);

        /*
         * The screen is shown before the runtime of the run is created, which
         * is the heavy part: it loads and extracts the patch packages, mounts
         * the layers of the sandbox and mounts its registry. The name it
         * presents is the one the packer wrote into this program, which is the
         * name of the packaged application, and its image is a resource of this
         * program as well.
         *
         * A payload without the image resource is the normal case of a session
         * which sets no startup image, so the screen shows the name and the
         * progress only instead of failing the run.
         */
        std::string_view startup_image;
        if (LauncherApp().launcher_config.startup.screen)
        {
            std::string image_error;
            if (!appbox::ReadEmbeddedResource(nullptr, IDR_APPBOX_STARTUP_IMAGE, startup_image, image_error))
            {
                SPDLOG_DEBUG("the startup screen has no image: {}", image_error);
            }
        }

        splash = std::make_unique<appbox::SplashScreen>(LauncherApp().launcher_config.startup.screen,
                                                        appbox::ReadOwnDisplayName(), startup_image);
        LauncherApp().splash = splash.get();

        /*
         * The screen is a fact of the run as well: the state query reports
         * whether the session shows one and which image it decoded, so a client
         * reads the screen of the run without looking at the window.
         */
        LauncherApp().RecordStartupScreen(*splash);

        /*
         * A self-extracting program carries the resources of the application
         * behind its own image. The payload is extracted into a temporary
         * folder of the run, which becomes the resource root of the sandbox:
         * the state directory, the patch directory and the logs stay beside
         * the program itself.
         */
        std::string payload_error;
        if (!appbox::ExtractProgramPayload(
                appbox::GetExecutablePath(),
                [](const appbox::ExtractProgress& report) {
                    const double total = report.total > 0 ? static_cast<double>(report.total) : 1.0;
                    ReportStartupStage(appbox::StartupStage::ExtractingPayload,
                                       static_cast<double>(report.done) / total);
                    return true;
                },
                extraction.Root(), payload_error))
        {
            throw std::runtime_error(payload_error);
        }

        if (!extraction.Root().empty())
        {
            LauncherApp().sandbox_paths.SetResourceRoot(
                (std::filesystem::path(extraction.Root()) / appbox::layout::kAppDirNameW).wstring());

            /* The folder is a fact of the run, which its state query reports. */
            LauncherApp().RecordExtractionRoot(appbox::WideToUTF8(extraction.Root()));
        }

        ReportStartupStage(appbox::StartupStage::Preparing, 1.0);

        LauncherApp().runtime = std::make_shared<AppBoxLauncherRuntime>(opt.log_level, opt.log_dir, pipe);

        /*
         * The sandbox exists now, so a request of another launcher may start
         * its programs: a request which arrived before this point waited for
         * the control thread, which the state query of the run reports.
         */
        LauncherApp().run_facts.prepared.store(true);

        /*
         * The view of the run is built from the injection data, which exists
         * once the sandbox is mounted: the initial targets and every request of
         * another launcher resolve their programs through it.
         */
        control->BindInjectData(LauncherApp().runtime->inject_data);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR(e.what());
        CloseStartupScreen(splash);
        StopServing();
        LauncherApp().control.reset();
        return 1;
    }

    /*
     * The startup files are selected before the first process starts, so a
     * rejected trigger is reported before anything runs.
     */
    SelectStartupsForRun(opt);

    if (!LauncherApp().startup_error.empty())
    {
        SPDLOG_ERROR("{}", LauncherApp().startup_error);
        LauncherApp().runtime.reset();
        CloseStartupScreen(splash);
        StopServing();
        LauncherApp().control.reset();
        return 1;
    }

    const auto exit_code = MainLauncher(splash);

    CloseStartupScreen(splash);
    LauncherApp().runtime.reset();
    StopServing();
    LauncherApp().control.reset();
    return exit_code;
}

/**
 * @brief Hand the request of this run over to the master instance of its sandbox.
 *
 * The second instance prepares nothing: it neither extracts a payload nor
 * releases the injection modules, it creates no state directory, it rebuilds no
 * registry image, it serves no pipe and it starts no program of its own. It
 * asks the master instance of the sandbox to start what its own command line
 * selects and leaves as soon as the master instance answered.
 *
 * @param[in] opt The command line options of the run.
 * @param[in] pipe Name of the control pipe of the sandbox.
 * @return The outcome of the handshake: the exit code of the run, or the request
 *         to race for the control pipe again because the sandbox is closing.
 */
static appbox::SecondaryHandover HandOverToMaster(const appbox::CommandLineOptions& opt, const std::string& pipe)
{
    const auto selection = appbox::MakeLauncherStartSelection(opt.shell, opt.has_startup_trigger, opt.startup_trigger);

    const auto request = appbox::BuildLauncherStartRequest(LauncherApp().launcher_config,
                                                           LauncherApp().sandbox_paths.root, selection, opt.extra_args);

    SPDLOG_INFO("join the sandbox '{}' of '{}'", request.sandbox_id, request.sandbox_root);

    return appbox::RunSecondaryInstance(pipe, request);
}

/**
 * @brief Run the launcher.
 *
 * The launcher has no user interface and no message loop: it resolves the
 * configuration, races for the control pipe of its sandbox and then either
 * serves the sandbox or hands its request over to the launcher which does. A
 * failure is logged and reported through the exit code.
 *
 * @return The exit code of the run.
 */
static DWORD RunLauncher()
{
    appbox::WinCallInit();

    appbox::CommandLineOptions opt;
    opt.ParseOptions();

    try
    {
        if (!opt.override_config.is_null())
        {
            LauncherApp().launcher_config = opt.override_config;
            ResolveSandboxPaths(opt.config_dir);
        }
        else
        {
            LoadConfig();
        }
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR(e.what());
        return 1;
    }

    /*
     * The identifier of the sandbox is checked before anything of the run is
     * created: a configuration which cannot name its sandbox starts no process,
     * creates no `data` directory and opens no pipe.
     */
    const auto id_error = CheckSandboxId();
    if (id_error != 0)
    {
        return id_error;
    }

    const auto pipe = appbox::SandboxPipeName(LauncherApp().launcher_config.sandbox_id);
    if (pipe.empty())
    {
        SPDLOG_ERROR("the sandbox identifier cannot name a control pipe, the run is refused");
        return ERROR_INVALID_DATA;
    }

    appbox::RemoteAccepter::Options options;
    options.first_instance = true;

    bool          logging_ready = false;
    std::uint32_t budget = appbox::kShutdownRaceBudgetMs;
    std::uint32_t delay = appbox::kShutdownRaceFirstDelayMs;

    /*
     * The race for the control pipe decides the role of this run: the launcher
     * which creates the first instance of the pipe serves the sandbox, and every
     * other launcher of the same sandbox hands its request over to it.
     *
     * A handshake which finds a sandbox which is closing is no failure: the run
     * waits for the interval of the shutdown race and races again, so the
     * request is served by the sandbox which is started next, or by this run
     * once it won the race itself.
     */
    for (;;)
    {
        const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, options);

        /*
         * The log files are set up once the role of the run is known: the options
         * of the run have no side effect on the sandbox before that, so a run
         * which refuses its configuration leaves nothing behind. A run which
         * races again keeps the log it opened for the first race.
         */
        if (!logging_ready)
        {
            opt.SetupLogging();
            SPDLOG_INFO("Load config: {}", nlohmann::json(LauncherApp().launcher_config).dump());
            logging_ready = true;
        }

        switch (created.status)
        {
        case appbox::ServerCreateStatus::Created:
            return RunMasterInstance(opt, created.accepter, pipe);

        case appbox::ServerCreateStatus::AlreadyRunning: {
            const auto handover = HandOverToMaster(opt, pipe);
            if (!handover.retry)
            {
                return handover.exit_code;
            }

            const auto retry = appbox::NextShutdownRetry(budget, delay);
            if (retry.delay_ms == 0)
            {
                SPDLOG_ERROR("the sandbox serves no request and does not come back within {} ms", budget);
                return ERROR_TIMEOUT;
            }

            SPDLOG_INFO("the sandbox ends its run, race for its control pipe again in {} ms", retry.delay_ms);
            Sleep(retry.delay_ms);

            budget = retry.remaining_ms;
            delay = retry.next_delay_ms;
            continue;
        }

        case appbox::ServerCreateStatus::AccessDenied:
            SPDLOG_ERROR("the control pipe '{}' of the sandbox cannot be used by this user", pipe);
            return ERROR_ACCESS_DENIED;

        case appbox::ServerCreateStatus::Failed:
        default:
            SPDLOG_ERROR("the control pipe '{}' of the sandbox cannot be created: {}", pipe, created.error_code);
            return created.error_code != 0 ? created.error_code : ERROR_GEN_FAILURE;
        }
    }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return static_cast<int>(RunLauncher());
}
