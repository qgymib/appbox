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

static void MainLoader()
{
    appbox::Defer defer([]() { wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI)); });

    if (!wxGetApp().startup_error.empty())
    {
        /* The selection failed already, the error was reported by OnInit(). */
        return;
    }

    /*
     * Every selected file is started before the first one is waited for, so
     * the startup files of the application run side by side instead of one
     * after the other.
     */
    std::vector<std::unique_ptr<appbox::ProcessJob>> jobs;
    DWORD                                            exit_code = 0;

    for (const auto* startup : wxGetApp().startups)
    {
        auto exe_path = appbox::UTF8ToWide(startup->executable.c_str());
        exe_path = appbox::ExpandKnownFolder(exe_path);

        auto job =
            std::make_unique<appbox::ProcessJob>(exe_path, BuildCmdArg(*startup), wxGetApp().runtime->inject_data);
        const auto ret = job->Start();
        if (ret != 0)
        {
            SPDLOG_ERROR("Failed to start the startup file '{}': {}", startup->trigger, ret);
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
 * @brief Format path as absolute path.
 * @param[in] path Path to format.
 * @param[in] dir Working directory.
 * @return Formatted absolute path.
 */
static std::wstring FormatPathAsAbs(const std::wstring& path, const std::wstring& dir)
{
    std::filesystem::path p(path);
    if (p.is_absolute())
    {
        return path;
    }

    std::filesystem::path folder(dir);
    auto                  d = std::filesystem::absolute(folder / path);

    return d.wstring();
}

/**
 * @brief Format config path as absolute path.
 * @param[in,out] cfg Config to format.
 * @param[in] dir Directory contains this config.
 */
static void FormatConfigAbsolutePath(appbox::LoaderConfig& cfg, const std::wstring& dir)
{
    {
        std::vector<std::string> abs_base_fs;
        for (const auto& fs : cfg.base_fs)
        {
            auto path = FormatPathAsAbs(appbox::UTF8ToWide(fs), dir);
            abs_base_fs.push_back(appbox::WideToUTF8(path));
        }
        cfg.base_fs = abs_base_fs;
    }
    {
        auto abs_overlay_fs = FormatPathAsAbs(appbox::UTF8ToWide(cfg.overlay_fs), dir);
        cfg.overlay_fs = appbox::WideToUTF8(abs_overlay_fs);
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
    FormatConfigAbsolutePath(wxGetApp().loader_config, dir);
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
            FormatConfigAbsolutePath(wxGetApp().loader_config, opt.config_dir);
        }
        else
        {
            LoadConfig();
        }
        FinializeCommandArgs(opt);
        SPDLOG_INFO("Load config: {}", nlohmann::json(wxGetApp().loader_config).dump());

        wxGetApp().runtime = std::make_shared<AppBoxLoaderRuntime>();
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
    if (!SelectStartups(wxGetApp().loader_config, opt.startup_trigger, opt.has_startup_trigger, wxGetApp().startups,
                        wxGetApp().startup_error))
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
