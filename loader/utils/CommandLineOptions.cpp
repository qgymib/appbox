#include "utils/MiniLauncher.hpp"
#include "CommandLineOptions.hpp"
#include "SetLogLevel.hpp"
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

/**
 * @brief Setup global file logger
 * @param[in] log_path Log file path
 */
static void SetupFileLog(const std::wstring& log_path)
{
    auto logger = spdlog::basic_logger_mt("Loader", log_path.c_str());
    spdlog::set_default_logger(logger);
}

static void SetupConfigFile(appbox::CommandLineOptions& opt, const std::wstring& arg)
{
    std::filesystem::path path(arg);
    opt.config_dir = std::filesystem::path(arg).parent_path().wstring();

    std::ifstream f(path);
    opt.override_config = nlohmann::json::parse(f);
}

static void RunAsStarter(CLI::App& app)
{
    auto remain_args = app.remaining();
    auto remain_args_sz = remain_args.size();

    std::wstring              exe_path;
    std::vector<std::wstring> exe_args;
    for (size_t i = 0; i < remain_args_sz; i++)
    {
        if (i == 0)
        {
            exe_path = CLI::widen(remain_args[i]);
        }
        else
        {
            auto arg = CLI::widen(remain_args[i]);
            exe_args.push_back(arg);
        }
    }

    exit(appbox::MiniLauncer(exe_path, exe_args));
}

appbox::CommandLineOptions::CommandLineOptions()
{
    wargc = 0;
    wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    is_launcher = false;
}

appbox::CommandLineOptions::~CommandLineOptions()
{
    LocalFree(wargv);
}

bool appbox::CommandLineOptions::ParseOptions()
{
    CLI::App app;
    app.prefix_command();
    app.add_option_function<std::wstring>("--X-AppBox-LogLevel", appbox::SetLogLevel, "Set application log level");
    app.add_option_function<std::wstring>("--X-AppBox-LogFile", SetupFileLog, "Set application log file");
    app.add_option_function<std::wstring>(
        "--X-AppBox-ConfigFile", [this](const std::wstring& arg) { SetupConfigFile(*this, arg); },
        "Use config file instead of loading builtin config file");
    app.add_option("--X-AppBox-Launcher", is_launcher, "Run as minilauncher");

    CLI11_PARSE(app, wargc, wargv);

    if (is_launcher)
    {
        RunAsStarter(app);
        return false;
    }

    for (auto& arg : app.remaining())
    {
        extra_args.push_back(arg);
    }

    return true;
}
