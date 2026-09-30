#include <wx/wx.h>
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
#include "utils/Defer.hpp"
#include "utils/GetExecutableDir.hpp"
#include "utils/ProcessJob.hpp"
#include "utils/Shell.hpp"
#include "utils/KnownFolder.hpp"
#include "utils/WinCall.hpp"
#include "widget/MainFrame.hpp"
#include "BuildCommandLine.hpp"
#include "Loader.hpp"
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
static std::vector<std::wstring> BuildCmdArg(const appbox::LoaderStartup& startup)
{
    std::vector<std::wstring> args;
    for (auto& arg : startup.arguments)
    {
        args.push_back(appbox::UTF8ToWide(arg));
    }

    return args;
}

/**
 * @brief Select the startup files the loader runs.
 *
 * A trigger given on the command line selects exactly one startup file and
 * suppresses the auto start of every other file. Without a trigger every
 * startup file which carries the auto start flag is selected; a configuration
 * without such a file is an error, because the sandbox would start nothing.
 *
 * @param[in] config The loader configuration.
 * @param[in] trigger Trigger given on the command line.
 * @param[in] has_trigger Whether a trigger was given on the command line.
 * @param[out] selected The startup files to run, in configuration order.
 * @param[out] error Error description on failure.
 * @return true when at least one startup file was selected.
 */
static bool SelectStartups(const appbox::LoaderConfig& config, const std::string& trigger, bool has_trigger,
                           std::vector<const appbox::LoaderStartup*>& selected, std::string& error)
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
 * of the loader, which `BuildShellArguments()` turns into the arguments of the
 * shell.
 *
 * @param[in] params Command of the shell, in UTF-8; empty to run an
 *                   interactive shell.
 * @return true when the shell was resolved, false when the machine offers no
 *         shell to run.
 */
static bool PrepareShell(const std::vector<std::string>& params)
{
    wxGetApp().shell = true;

    wxGetApp().shell_path = appbox::ResolveShellPath();
    if (wxGetApp().shell_path.empty())
    {
        return false;
    }

    wxGetApp().shell_args = appbox::BuildShellArguments(params);
    SPDLOG_INFO("Run the shell '{}' of the host in the sandbox", appbox::WideToUTF8(wxGetApp().shell_path));
    return true;
}

/**
 * @brief Select the startup files the loader runs.
 *
 * The selection happens on the main thread, so a rejected trigger is reported
 * before the working thread starts.
 *
 * A run of the shell selects no startup file at all: the sandbox of the run is
 * described by the configuration, while the shell replaces the application the
 * configuration starts. A configuration without a startup file is therefore
 * not an error for such a run.
 *
 * @param[in] opt The command line options of the loader.
 */
static void SelectStartupsForRun(const appbox::CommandLineOptions& opt)
{
    if (opt.shell)
    {
        return;
    }

    SelectStartups(wxGetApp().loader_config, opt.startup_trigger, opt.has_startup_trigger, wxGetApp().startups,
                   wxGetApp().startup_error);
}

/**
 * @brief One process the loader starts inside the sandbox.
 */
struct LaunchTarget
{
    std::string               name;         /* Name of the target, for the log. */
    std::wstring              exe_path;     /* Executable to run. */
    std::vector<std::wstring> args;         /* Arguments of the executable. */
    bool                      hide_console; /* Start the process without a console window. */
};

/**
 * @brief Build the processes the loader starts.
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

    if (wxGetApp().shell)
    {
        LaunchTarget target;
        target.name = "shell";
        target.exe_path = wxGetApp().shell_path;
        target.args = wxGetApp().shell_args;
        target.hide_console = false;
        targets.push_back(std::move(target));
        return targets;
    }

    for (const auto* startup : wxGetApp().startups)
    {
        LaunchTarget target;
        target.name = startup->trigger;
        target.exe_path = appbox::ExpandKnownFolder(appbox::UTF8ToWide(startup->executable.c_str()));
        target.args = BuildCmdArg(*startup);
        target.hide_console = wxGetApp().loader_config.hide_console;
        targets.push_back(std::move(target));
    }

    return targets;
}

static void MainLoader()
{
    appbox::Defer defer([]() { wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI)); });

    if (!wxGetApp().startup_error.empty())
    {
        /* The selection failed already, the error was reported by OnInit(). */
        return;
    }

    const auto targets = BuildLaunchTargets();

    /*
     * Every target is started before the first one is waited for, so the
     * targets of the run side by side instead of one after the other.
     */
    std::vector<std::unique_ptr<appbox::ProcessJob>> jobs;
    DWORD                                            exit_code = 0;

    for (const auto& target : targets)
    {
        auto job = std::make_unique<appbox::ProcessJob>(target.exe_path, target.args, wxGetApp().runtime->inject_data,
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

    wxGetApp().exit_code = exit_code;
    SPDLOG_INFO("the sandboxed application exited with code {}", wxGetApp().exit_code);
}

/**
 * @brief Resolve the layout of the sandbox.
 *
 * The resources of the packed application and the state of the sandbox live in
 * the fixed directories of `common/SandboxLayout.hpp`, resolved against the
 * directory of the configuration: the directory of the loader program itself
 * unless a configuration file was given on the command line.
 *
 * @param[in] config_dir Directory which holds the configuration, empty to use
 *                       the directory of the loader program.
 */
static void ResolveSandboxPaths(const std::wstring& config_dir)
{
    const auto dir = config_dir.empty() ? appbox::GetExecutableDir() : config_dir;
    wxGetApp().sandbox_paths = appbox::SandboxPaths::Resolve(dir);
}

/**
 * @brief Refuse a run whose sandbox injection modules are missing.
 *
 * The modules are resources of the archive and the loader injects them from
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
    const auto& paths = wxGetApp().sandbox_paths;

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
        throw std::runtime_error("the loader configuration file was not found: " + appbox::WideToUTF8(path));
    }

    nlohmann::json j_cfg = nlohmann::json::parse(f);
    wxGetApp().loader_config = j_cfg;
    ResolveSandboxPaths(dir);
}

static void FinializeCommandArgs(const appbox::CommandLineOptions& opt)
{
    /*
     * The arguments which were not consumed by the loader belong to the
     * sandboxed application, so every startup file receives them.
     */
    for (auto& startup : wxGetApp().loader_config.startups)
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
 * arguments of the loader are the command of the shell instead of the
 * arguments of a startup file, and the startup files of the configuration are
 * ignored by the run.
 *
 * @param[in] opt The command line options of the loader.
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
        wxGetApp().startup_error = "the options --X-AppBox-Shell and --X-AppBox-Startup cannot be used together";
        return false;
    }

    if (!PrepareShell(opt.extra_args))
    {
        wxGetApp().startup_error = "the shell of the host could not be resolved";
        return false;
    }

    return true;
}

bool AppBoxLoader::OnInit()
{
    appbox::WinCallInit();

    appbox::CommandLineOptions opt;
    if (!opt.ParseOptions())
    {
        return false;
    }

    try
    {
        if (!opt.override_config.is_null())
        {
            wxGetApp().loader_config = opt.override_config;
            ResolveSandboxPaths(opt.config_dir);
        }
        else
        {
            LoadConfig();
        }
        PrepareShellRun(opt);
        SPDLOG_INFO("Load config: {}", nlohmann::json(wxGetApp().loader_config).dump());

        /*
         * A run without the injection modules cannot sandbox anything: the
         * failure is reported by the block below the startup selection, which
         * turns it into a non zero exit code and starts nothing.
         */
        CheckSandboxModules(wxGetApp().startup_error);

        wxGetApp().runtime = std::make_shared<AppBoxLoaderRuntime>(opt.log_level);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR(e.what());
        wxGenericMessageDialog dlg(nullptr, e.what(), "Error", wxOK | wxICON_ERROR);
        dlg.ShowModal();
        return false;
    }

    /*
     * The startup files are selected on the main thread, so a rejected
     * trigger is reported before the working thread starts. The failure is
     * always logged and turns into a non zero exit code; the dialog is shown
     * with the admin UI only, so an unattended run cannot wait for a click.
     */
    SelectStartupsForRun(opt);

    if (!wxGetApp().startup_error.empty())
    {
        SPDLOG_ERROR("{}", wxGetApp().startup_error);
        this->exit_code = 1;

        if (this->loader_config.enable_admin_ui)
        {
            wxGenericMessageDialog dlg(nullptr, wxGetApp().startup_error, "Error", wxOK | wxICON_ERROR);
            dlg.ShowModal();
        }
    }

    this->Bind(APPBOX_EXIT_APPLICATION_IF_NO_GUI, &AppBoxLoader::HandleEventExitApplicationNoGUI, this);

    main_frame = new MainFrame(wxGetApp().runtime->inject_data.registry_hive_dos_path);
    main_frame->Show(this->loader_config.enable_admin_ui);

    this->working_thread = new std::thread(MainLoader);
    return true;
}

int AppBoxLoader::OnExit()
{
    if (this->working_thread != nullptr)
    {
        this->working_thread->join();
        delete this->working_thread;
        this->working_thread = nullptr;
    }

    runtime.reset();
    return wxApp::OnExit();
}

int AppBoxLoader::OnRun()
{
    auto ret = wxApp::OnRun();
    return ret != 0 ? ret : static_cast<int>(exit_code);
}

void AppBoxLoader::HandleEventExitApplicationNoGUI(wxCommandEvent&)
{
    /*
     * Only close application if the main frame is not shown.
     */
    if (!main_frame->IsShown())
    {
        main_frame->Close();
    }
}

wxIMPLEMENT_APP(AppBoxLoader); // NOLINT
