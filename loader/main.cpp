#include <wx/wx.h>
#include <CLI/CLI.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <detours.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <base64.hpp>
#include "utils/GetExecutableDir.hpp"
#include "widget/MainFrame.hpp"
#include "Defines.hpp"
#include "BuildCommandLine.hpp"
#include "SetLogLevel.hpp"
#include "Loader.hpp"
#include "WString.hpp"

struct CommandLineOptions
{
    CommandLineOptions()
    {
        wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    }

    ~CommandLineOptions()
    {
        LocalFree(wargv);
    }

    int     wargc = 0;
    LPWSTR* wargv = nullptr;

    std::wstring             config_dir;      /* Config file directory path */
    nlohmann::json           override_config; /* Override config */
    std::vector<std::string> extra_args;      /* Extra arguments, encoding in UTF-8 */
};

static std::wstring BuildCmdArg(const std::wstring& exe_path)
{
    std::vector<std::wstring> args;
    for (auto& arg : wxGetApp().loader_config.launch.arguments)
    {
        args.push_back(appbox::UTF8ToWide(arg));
    }

    return appbox::BuildCommandLine(exe_path, args);
}

static void MainLoader()
{
#if defined(_WIN64)
    auto sandbox_dll_path = wxGetApp().runtime->inject_data.sandbox64_dos_path;
#else
    auto sandbox_dll_path = wxGetApp().runtime->inject_data.sandbox32_dos_path;
#endif

    const GUID guid = SANDBOX_GUID;

    STARTUPINFOW startupInfo;
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    memset(&processInfo, 0, sizeof(processInfo));

    auto exe_path = appbox::UTF8ToWide(wxGetApp().loader_config.launch.executable.c_str());
    auto cmdline = BuildCmdArg(exe_path);
    SPDLOG_TRACE(L"cmd: {}", cmdline);

    if (!DetourCreateProcessWithDllExW(exe_path.c_str(), cmdline.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                                       nullptr, nullptr, &startupInfo, &processInfo, sandbox_dll_path.c_str(), nullptr))
    {
        SPDLOG_ERROR("DetourCreateProcessWithDllExW(): failed");
        wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI));
        return;
    }

    std::string inject_data = nlohmann::json(wxGetApp().runtime->inject_data).dump();
    if (!DetourCopyPayloadToProcess(processInfo.hProcess, guid, inject_data.c_str(),
                                    static_cast<DWORD>(inject_data.size())))
    {
        SPDLOG_ERROR("DetourCopyPayloadToProcess(): failed");
        wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI));
        return;
    }

    /* Start and wait for process exit. */
    {
        ResumeThread(processInfo.hThread);
        CloseHandle(processInfo.hThread);
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        GetExitCodeProcess(processInfo.hProcess, &wxGetApp().exit_code);
        CloseHandle(processInfo.hProcess);
    }

    SPDLOG_INFO("application exited with code {}", wxGetApp().exit_code);
    wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI));
}

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

static void FormatConfigPath(appbox::LoaderConfig& cfg, const std::wstring& dir)
{
    {
        auto abs_base_fs = FormatPathAsAbs(appbox::UTF8ToWide(cfg.base_fs), dir);
        cfg.base_fs = appbox::WideToUTF8(abs_base_fs);
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
    FormatConfigPath(wxGetApp().loader_config, dir);
}

/**
 * @brief Setup global file logger
 * @param[in] log_path Log file path
 */
static void SetupFileLog(const std::wstring& log_path)
{
    auto logger = spdlog::basic_logger_mt("Loader", log_path.c_str());
    spdlog::set_default_logger(logger);
}

static void SetupConfigFile(CommandLineOptions& opt, const std::wstring& arg)
{
    std::filesystem::path path(arg);
    opt.config_dir = std::filesystem::path(arg).parent_path().wstring();

    std::ifstream f(path);
    opt.override_config = nlohmann::json::parse(f);
}

static int ParseCommandLine(CommandLineOptions& opt)
{
    CLI::App app;
    app.prefix_command();
    app.add_option_function<std::wstring>("--X-AppBox-LogLevel", appbox::SetLogLevel, "Set application log level");
    app.add_option_function<std::wstring>("--X-AppBox-LogFile", SetupFileLog, "Set application log file");
    app.add_option_function<std::wstring>(
        "--X-AppBox-ConfigFile", [&opt](const std::wstring& arg) { SetupConfigFile(opt, arg); },
        "Use config file instead of loading builtin config file");

    CLI11_PARSE(app, opt.wargc, opt.wargv);
    for (auto& arg : app.remaining())
    {
        opt.extra_args.push_back(arg);
    }

    return 0;
}

static void FinializeCommandArgs(const CommandLineOptions& opt)
{
    for (auto& arg : opt.extra_args)
    {
        wxGetApp().loader_config.launch.arguments.push_back(arg);
    }
}

bool AppBoxLoader::OnInit()
{
    CommandLineOptions opt;
    if (ParseCommandLine(opt) != 0)
    {
        return false;
    }

    try
    {
        if (!opt.override_config.is_null())
        {
            wxGetApp().loader_config = opt.override_config;
            FormatConfigPath(wxGetApp().loader_config, opt.config_dir);
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
        wxMessageBox(e.what(), "Error", wxOK | wxICON_ERROR);
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
