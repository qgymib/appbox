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

    nlohmann::json override_config; /* Override config */
};

static void MainLoader()
{
#if defined(_WIN64)
    auto& sandbox_dll_path = wxGetApp().runtime->inject_data.sandbox64_path;
#else
    auto& sandbox_dll_path = wxGetApp().runtime->inject_data.sandbox32_path;
#endif

    const GUID guid = SANDBOX_GUID;

    STARTUPINFOW startupInfo;
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    memset(&processInfo, 0, sizeof(processInfo));

    auto exe_path = appbox::UTF8ToWide(wxGetApp().config.launch.executable.c_str());
    auto cmdline = appbox::BuildCommandLine(exe_path, wxGetApp().exe_args);
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
    DWORD exit_code = 0;
    {
        ResumeThread(processInfo.hThread);
        CloseHandle(processInfo.hThread);
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        GetExitCodeProcess(processInfo.hProcess, &exit_code);
        CloseHandle(processInfo.hProcess);
    }

    SPDLOG_INFO("application exited");
    wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI));
}

static void LoadConfig()
{
    /* Parse configuration file. */
    {
        auto dir = appbox::GetExecutableDir();
        auto path = std::filesystem::path(dir) / L"AppBoxLoader.json";

        std::ifstream  f(path);
        nlohmann::json config = nlohmann::json::parse(f);
        SPDLOG_DEBUG("{}", config.dump());

        wxGetApp().config = config;
    }

    /* Convert executable parameters from config */
    for (auto& arg : wxGetApp().config.launch.arguments)
    {
        auto warg = appbox::UTF8ToWide(arg.c_str());
        wxGetApp().exe_args.push_back(warg);
    }
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

static void SetupConfigBase64(CommandLineOptions& opt, const std::wstring& arg)
{
    auto argu8 = CLI::narrow(arg);
    auto data = base64::from_base64(argu8);
    opt.override_config = nlohmann::json::parse(data);
}

static void SetupConfigFile(CommandLineOptions& opt, const std::wstring& arg)
{
    auto          path = std::filesystem::path(arg);
    std::ifstream f(path);
    opt.override_config = nlohmann::json::parse(f);
}

static int ParseCommandLine(CommandLineOptions& opt)
{
    CLI::App app;
    app.prefix_command();
    app.add_option_function<std::wstring>("--X-AppBox-LogLevel", appbox::SetLogLevel, "Set application log level");
    app.add_option_function<std::wstring>("--X-AppBox-LogFile", SetupFileLog, "Set application log file");
    auto opt_config_base64 = app.add_option_function<std::wstring>(
        "--X-AppBox-ConfigBase64", [&opt](const std::wstring& arg) { SetupConfigBase64(opt, arg); },
        "Use base64 encoded config file instead of loading builtin config file");
    auto opt_config_file = app.add_option_function<std::wstring>(
        "--X-AppBox-ConfigFile", [&opt](const std::wstring& arg) { SetupConfigFile(opt, arg); },
        "Use config file instead of loading builtin config file");

    opt_config_base64->excludes(opt_config_file);

    CLI11_PARSE(app, opt.wargc, opt.wargv);
    for (auto& arg : app.remaining())
    {
        wxGetApp().exe_args.push_back(CLI::widen(arg));
    }

    return 0;
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
            SPDLOG_INFO("override config: {}", opt.override_config.dump());
            wxGetApp().config = opt.override_config;
        }
        else
        {
            LoadConfig();
        }

        wxGetApp().runtime = std::make_shared<AppBoxLoaderRuntime>();
    }
    catch (const std::exception& e)
    {
        wxMessageBox(e.what(), "Error", wxOK | wxICON_ERROR);
        return false;
    }

    this->Bind(APPBOX_EXIT_APPLICATION_IF_NO_GUI, &AppBoxLoader::HandleEventExitApplicationNoGUI, this);

    main_frame = new MainFrame;
    main_frame->Show(this->config.enable_admin_ui);

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
