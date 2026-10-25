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
#include <cctype>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <base64.hpp>
#include "EmbeddedResource.hpp"
#include "sandbox/utils/Defines.hpp"
#include "utils/CommandLineOptions.hpp"
#include "utils/GetExecutableDir.hpp"
#include "utils/ProcessJob.hpp"
#include "utils/SandboxModules.hpp"
#include "utils/Shell.hpp"
#include "utils/KnownFolder.hpp"
#include "utils/VersionInfo.hpp"
#include "utils/ViewPathResolver.hpp"
#include "utils/WinCall.hpp"
#include "EmbeddedResourceIds.h"
#include "Launcher.hpp"
#include "Splash.hpp"
#include "WString.hpp"

/**
 * @brief Compare two trigger names ignoring case.
 * @param[in] a Left operand.
 * @param[in] b Right operand.
 * @return true when both names are equal ignoring case.
 */
static bool EqualsIgnoreCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const auto left = std::tolower(static_cast<unsigned char>(a[i]));
        const auto right = std::tolower(static_cast<unsigned char>(b[i]));
        if (left != right)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Get the command line arguments of one startup file.
 * @param[in] startup Startup file to convert.
 * @return The arguments in wide characters.
 */
static std::vector<std::wstring> BuildCmdArg(const appbox::LauncherStartup& startup)
{
    std::vector<std::wstring> args;
    for (auto& arg : startup.arguments)
    {
        args.push_back(appbox::UTF8ToWide(arg));
    }

    return args;
}

/**
 * @brief Select the startup files the launcher runs.
 *
 * A trigger given on the command line selects exactly one startup file and
 * suppresses the auto start of every other file. Without a trigger every
 * startup file which carries the auto start flag is selected; a configuration
 * without such a file is an error, because the sandbox would start nothing.
 *
 * @param[in] config The launcher configuration.
 * @param[in] trigger Trigger given on the command line.
 * @param[in] has_trigger Whether a trigger was given on the command line.
 * @param[out] selected The startup files to run, in configuration order.
 * @param[out] error Error description on failure.
 * @return true when at least one startup file was selected.
 */
static bool SelectStartups(const appbox::LauncherConfig& config, const std::string& trigger, bool has_trigger,
                           std::vector<const appbox::LauncherStartup*>& selected, std::string& error)
{
    selected.clear();

    if (has_trigger)
    {
        for (const auto& startup : config.startups)
        {
            if (EqualsIgnoreCase(startup.trigger, trigger))
            {
                selected.push_back(&startup);
                return true;
            }
        }

        error = "no startup file uses the trigger '" + trigger + "'";
        return false;
    }

    for (const auto& startup : config.startups)
    {
        if (startup.auto_start)
        {
            selected.push_back(&startup);
        }
    }

    if (selected.empty())
    {
        error = "no startup file is marked for auto start";
        return false;
    }

    return true;
}

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

    SelectStartups(LauncherApp().launcher_config, opt.startup_trigger, opt.has_startup_trigger, LauncherApp().startups,
                   LauncherApp().startup_error);
}

/**
 * @brief One process the launcher starts inside the sandbox.
 */
struct LaunchTarget
{
    std::string               name;         /* Name of the target, for the log. */
    std::wstring              exe_path;     /* Executable to run, as the configuration spells it. */
    std::wstring              image_path;   /* Host path of the image which is created. */
    std::vector<std::wstring> args;         /* Arguments of the executable. */
    bool                      hide_console; /* Start the process without a console window. */
    std::string               error;        /* Reason the target cannot be started at all. */
};

/**
 * @brief Resolve the image of one target into the path of the host.
 *
 * The configuration names a path of the sandbox view, which usually exists in
 * the layers of the archive only: the launcher resolves it before it creates
 * the process, because the process has to be created from the file which holds
 * the image. A path which no layer and no host path holds refuses the target.
 *
 * @param[in] resolver Resolver of the view of the run.
 * @param[in,out] target Target whose image is resolved.
 */
static void ResolveTargetImage(const appbox::ViewPathResolver& resolver, LaunchTarget& target)
{
    if (!target.error.empty())
    {
        /* The kind of the startup file already refuses the run. */
        return;
    }

    resolver.Resolve(target.exe_path, target.image_path, target.error);
}

/**
 * @brief Build the processes the launcher starts.
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
 * The image of a program is the file of the layer which holds it, while the
 * configuration names the path of the view: the run resolves both the shell and
 * the startup files before it creates the first process, so a program the view
 * does not hold refuses the run instead of failing while it starts.
 *
 * @return The processes to start, in the order they are started in.
 */
static std::vector<LaunchTarget> BuildLaunchTargets()
{
    std::vector<LaunchTarget> targets;

    /*
     * The view of the run: the layers the launcher mounts and the isolation
     * modes of the archive. The resolver decides a path with the rules the
     * sandbox applies to a path of an application, so the launcher creates the
     * process from the very file the application would open.
     */
    const appbox::ViewPathResolver resolver(LauncherApp().runtime->inject_data);

    if (LauncherApp().shell)
    {
        LaunchTarget target;
        target.name = "shell";
        target.exe_path = LauncherApp().shell_path;
        target.args = LauncherApp().shell_args;
        target.hide_console = false;
        ResolveTargetImage(resolver, target);
        targets.push_back(std::move(target));
        return targets;
    }

    for (const auto* startup : LauncherApp().startups)
    {
        const auto path = appbox::ExpandKnownFolder(appbox::UTF8ToWide(startup->executable.c_str()));

        LaunchTarget target;
        target.name = startup->trigger;
        target.hide_console = LauncherApp().launcher_config.hide_console;
        appbox::BuildStartupInvocation(path, BuildCmdArg(*startup), target.exe_path, target.args, target.error);
        ResolveTargetImage(resolver, target);
        targets.push_back(std::move(target));
    }

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
}

/**
 * @brief Run the processes the configuration describes.
 *
 * Every target is started before the first one is waited for, so the targets of
 * the run side by side instead of one after the other.
 *
 * The startup screen of the run covers the creation of the processes and is
 * destroyed as soon as every process was started, which is the moment the
 * packaged application is on its own.
 *
 * @param[in,out] splash Startup screen of the run, which is closed by the call.
 * @return The exit code of the run: the first non zero exit code of a target,
 *         zero when every target exited with zero.
 */
static DWORD MainLauncher(std::unique_ptr<appbox::SplashScreen>& splash)
{
    const auto targets = BuildLaunchTargets();

    /*
     * Every target is started before the first one is waited for, so the
     * targets of the run side by side instead of one after the other.
     */
    std::vector<std::unique_ptr<appbox::ProcessJob>> jobs;
    DWORD                                            exit_code = 0;

    ReportStartupStage(appbox::StartupStage::StartingApplication, 0.0);

    for (const auto& target : targets)
    {
        if (!target.error.empty())
        {
            /*
             * The kind of the startup file could not be turned into a process,
             * which is a reason the run cannot work around: the interpreter it
             * needs is not on this machine, see BuildLaunchTargets().
             */
            SPDLOG_ERROR("Failed to start '{}': {}", target.name, target.error);
            if (exit_code == 0)
            {
                exit_code = ERROR_FILE_NOT_FOUND;
            }
            continue;
        }

        auto       job = std::make_unique<appbox::ProcessJob>(target.image_path, target.exe_path, target.args,
                                                              LauncherApp().runtime->inject_data, target.hide_console);
        const auto ret = job->Start();
        if (ret != 0)
        {
            SPDLOG_ERROR("Failed to start '{}': {}", target.name, ret);
            if (exit_code == 0)
            {
                exit_code = ret;
            }
            continue;
        }

        jobs.push_back(std::move(job));

        /* The screen follows the processes the run creates. */
        const auto created = static_cast<double>(jobs.size());
        const auto total = targets.empty() ? 1.0 : static_cast<double>(targets.size());
        ReportStartupStage(appbox::StartupStage::StartingApplication, created / total);
    }

    /*
     * Every process the run starts was created, so the screen has done its
     * job: a failure of one target is reported by the exit code of the run and
     * not by a window which stays open.
     */
    CloseStartupScreen(splash);

    for (auto& job : jobs)
    {
        const auto ret = job->Wait(INFINITE);
        if (ret != 0)
        {
            SPDLOG_ERROR("Failed to wait for process: {}", ret);
            if (exit_code == 0)
            {
                exit_code = ret;
            }
            continue;
        }

        const auto code = job->GetExitCode();
        SPDLOG_INFO("application exited with code {}", code);
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

static void FinializeCommandArgs(const appbox::CommandLineOptions& opt)
{
    /*
     * The arguments which were not consumed by the launcher belong to the
     * sandboxed application, so every startup file receives them.
     */
    for (auto& startup : LauncherApp().launcher_config.startups)
    {
        for (const auto& arg : opt.extra_args)
        {
            startup.arguments.push_back(arg);
        }
    }
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
        FinializeCommandArgs(opt);
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
 * @brief Run the launcher.
 *
 * The launcher has no user interface and no message loop: it resolves the
 * configuration, starts the startup files (or the shell) and exits with the
 * exit code of the run. A failure is logged and reported through the exit code.
 *
 * @return The exit code of the run.
 */
static DWORD RunLauncher()
{
    appbox::WinCallInit();

    appbox::CommandLineOptions opt;
    opt.ParseOptions();

    /*
     * The startup screen of the run. It covers the preparation of the sandbox
     * and disappears as soon as the processes of the run were started, so it
     * belongs to the run and is released by every path which leaves it.
     */
    std::unique_ptr<appbox::SplashScreen> splash;

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
        PrepareShellRun(opt);
        SPDLOG_INFO("Load config: {}", nlohmann::json(LauncherApp().launcher_config).dump());

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

        ReportStartupStage(appbox::StartupStage::Preparing, 1.0);

        LauncherApp().runtime = std::make_shared<AppBoxLauncherRuntime>(opt.log_level, opt.log_dir);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR(e.what());
        CloseStartupScreen(splash);
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
        return 1;
    }

    const auto exit_code = MainLauncher(splash);

    CloseStartupScreen(splash);
    LauncherApp().runtime.reset();
    return exit_code;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return static_cast<int>(RunLauncher());
}
