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
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <base64.hpp>
#include "sandbox/utils/Defines.hpp"
#include "utils/CommandLineOptions.hpp"
#include "utils/GetExecutableDir.hpp"
#include "utils/ProcessJob.hpp"
#include "utils/Shell.hpp"
#include "utils/KnownFolder.hpp"
#include "utils/WinCall.hpp"
#include "Launcher.hpp"
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
    std::wstring              exe_path;     /* Executable to run. */
    std::vector<std::wstring> args;         /* Arguments of the executable. */
    bool                      hide_console; /* Start the process without a console window. */
};

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
 * selected, in configuration order.
 *
 * @return The processes to start, in the order they are started in.
 */
static std::vector<LaunchTarget> BuildLaunchTargets()
{
    std::vector<LaunchTarget> targets;

    if (LauncherApp().shell)
    {
        LaunchTarget target;
        target.name = "shell";
        target.exe_path = LauncherApp().shell_path;
        target.args = LauncherApp().shell_args;
        target.hide_console = false;
        targets.push_back(std::move(target));
        return targets;
    }

    for (const auto* startup : LauncherApp().startups)
    {
        LaunchTarget target;
        target.name = startup->trigger;
        target.exe_path = appbox::ExpandKnownFolder(appbox::UTF8ToWide(startup->executable.c_str()));
        target.args = BuildCmdArg(*startup);
        target.hide_console = LauncherApp().launcher_config.hide_console;
        targets.push_back(std::move(target));
    }

    return targets;
}

/**
 * @brief Run the processes the configuration describes.
 *
 * Every target is started before the first one is waited for, so the targets of
 * the run side by side instead of one after the other.
 *
 * @return The exit code of the run: the first non zero exit code of a target,
 *         zero when every target exited with zero.
 */
static DWORD MainLauncher()
{
    const auto targets = BuildLaunchTargets();

    /*
     * Every target is started before the first one is waited for, so the
     * targets of the run side by side instead of one after the other.
     */
    std::vector<std::unique_ptr<appbox::ProcessJob>> jobs;
    DWORD                                            exit_code = 0;

    for (const auto& target : targets)
    {
        auto job = std::make_unique<appbox::ProcessJob>(target.exe_path, target.args, LauncherApp().runtime->inject_data,
                                                        target.hide_console);
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
    }

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
 * directory of the configuration: the directory of the launcher program itself
 * unless a configuration file was given on the command line.
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
 * @brief Refuse a run whose sandbox injection modules are missing.
 *
 * The modules are resources of the archive and the launcher injects them from
 * `app` instead of writing a copy into the state directory (see
 * `common/SandboxLayout.hpp`), so a run without them cannot sandbox anything:
 * the check happens before the runtime is created, which reports the missing
 * path instead of failing while the application is started.
 *
 * @param[out] error Description of the first module which was not found, left
 *                   untouched when both modules exist.
 */
static void CheckSandboxModules(std::string& error)
{
    const auto& paths = LauncherApp().sandbox_paths;

    const std::wstring modules[] = { paths.Sandbox32Dll(), paths.Sandbox64Dll() };
    for (const auto& module : modules)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(module, ec))
        {
            error = "the sandbox injection module was not found: " + appbox::WideToUTF8(module);
            return;
        }
    }
}

static void LoadConfig()
{
    /*
     * The configuration file is named after the executable itself and lives
     * beside it, so an executable `foo.exe` loads `foo.exe.json` from its own
     * directory. No fallback name is tried: a missing configuration file is
     * reported as an error.
     */
    auto dir = appbox::GetExecutableDir();
    auto path = appbox::DefaultConfigPathForExecutable(appbox::GetExecutablePath());

    std::ifstream f{ std::filesystem::path(path) };
    if (!f.is_open())
    {
        throw std::runtime_error("the launcher configuration file was not found: " + appbox::WideToUTF8(path));
    }

    nlohmann::json j_cfg = nlohmann::json::parse(f);
    LauncherApp().launcher_config = j_cfg;
    ResolveSandboxPaths(dir);
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
    if (!opt.ParseOptions())
    {
        /*
         * The process launcher mode leaves the process in RunAsStarter(); this
         * path is the plain "nothing to run" refusal of the option parser.
         */
        return 0;
    }

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
         * failure is reported by the block below the startup selection, which
         * turns it into a non zero exit code and starts nothing.
         */
        CheckSandboxModules(LauncherApp().startup_error);

        LauncherApp().runtime = std::make_shared<AppBoxLauncherRuntime>(opt.log_level);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR(e.what());
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
        return 1;
    }

    const auto exit_code = MainLauncher();
    LauncherApp().runtime.reset();
    return exit_code;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return static_cast<int>(RunLauncher());
}
