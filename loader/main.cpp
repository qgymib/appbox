#include <wx/wx.h>
#include <CLI/CLI.hpp>
#include <detours.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
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
#include "filesystem/RegCompile.hpp"
#include "Loader.hpp"
#include "WString.hpp"

static std::vector<std::wstring> BuildCmdArg()
{
    std::vector<std::wstring> args;
    for (auto& arg : wxGetApp().loader_config.launch.arguments)
    {
        args.push_back(appbox::UTF8ToWide(arg));
    }

    return args;
}

static void MainLoader()
{
    DWORD         ret;
    appbox::Defer defer([]() { wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI)); });

    auto exe_path = appbox::UTF8ToWide(wxGetApp().loader_config.launch.executable.c_str());
    exe_path = appbox::ExpandKnownFolder(exe_path);
    auto               cmdline = BuildCmdArg();
    appbox::ProcessJob job(exe_path, cmdline);

    if ((ret = job.Start()) != 0)
    {
        SPDLOG_ERROR("Failed to start process: {}", ret);
        return;
    }
    if ((ret = job.Wait(INFINITE)) != 0)
    {
        SPDLOG_ERROR("Failed to wait for process: {}", ret);
        return;
    }

    wxGetApp().exit_code = job.GetExitCode();
    SPDLOG_INFO("application exited with code {}", wxGetApp().exit_code);
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
    /* Parse configuration file. */
    auto dir = appbox::GetExecutableDir();
    auto cfg_name = appbox::GetExecutableName() + L".json";
    auto path = std::filesystem::path(dir) / cfg_name;

    std::ifstream  f(path);
    nlohmann::json j_cfg = nlohmann::json::parse(f);
    wxGetApp().loader_config = j_cfg;
    FormatConfigAbsolutePath(wxGetApp().loader_config, dir);
}

static void FinializeCommandArgs(const appbox::CommandLineOptions& opt)
{
    for (auto& arg : opt.extra_args)
    {
        wxGetApp().loader_config.launch.arguments.push_back(arg);
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
        wxGetApp().hive_path = CLI::widen(wxGetApp().loader_config.overlay_fs) + L"\\registry.hive";

        /* Initialize (compile/update) the registry hive from LowerFS registry.reg */
        {
            auto reg_result = appbox::InitializeRegistryHive(wxGetApp().hive_path, wxGetApp().loader_config.base_fs);
            if (!reg_result.success)
            {
                throw std::runtime_error(
                    fmt::format("registry.hive initialization failed: error={}", reg_result.error_code));
            }
        }
    }
    catch (const std::exception& e)
    {
        wxGenericMessageDialog dlg(nullptr, e.what(), "Error", wxOK | wxICON_ERROR);
        dlg.ShowModal();
        return false;
    }

    this->Bind(APPBOX_EXIT_APPLICATION_IF_NO_GUI, &AppBoxLoader::HandleEventExitApplicationNoGUI, this);

    main_frame = new MainFrame;
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
