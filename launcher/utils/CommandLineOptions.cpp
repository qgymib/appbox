#include "utils/ProcessLauncher.hpp"
#include "utils/GetExecutableDir.hpp"
#include "CommandLineOptions.hpp"
#include "LogFileName.hpp"
#include "SetLogLevel.hpp"
#include "WString.hpp"
#include <CLI/CLI.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{

/** Name of the directory the log files of a run land in by default. */
constexpr const wchar_t* kLogDirName = L"log";

/**
 * @brief Resolve the directory the log files of a run are written into.
 *
 * An empty value names the default directory, which is `log` beside the
 * launcher program itself: the log files of a run belong to the program which
 * wrote them and not to the directory the program happened to be started from.
 * A relative value is resolved against the current directory, like every other
 * relative path of a command line.
 *
 * @param[in] value Value of the option which names the directory, empty when
 *                  the option was not given.
 * @return The absolute path of the directory.
 */
std::wstring ResolveLogDir(const std::wstring& value)
{
    if (value.empty())
    {
        return (std::filesystem::path(appbox::GetExecutableDir()) / kLogDirName).wstring();
    }

    std::error_code ec;
    const auto      path = std::filesystem::absolute(value, ec);
    return ec ? value : path.wstring();
}

/**
 * @brief Build the name of the log file of the launcher itself.
 *
 * The name follows the rule of `common/LogFileName.hpp`, which the sandbox uses
 * for the log file of every process it is injected into, so the log files of
 * one run are recognized by a single name pattern.
 *
 * @return The file name, without a directory.
 */
std::wstring OwnLogFileName()
{
    SYSTEMTIME utc;
    ZeroMemory(&utc, sizeof(utc));
    ::GetSystemTime(&utc);

    const appbox::LogFileTimestamp timestamp{ utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond };
    return appbox::LogFileNameOf(appbox::GetExecutablePath(), timestamp, ::GetCurrentProcessId());
}

/**
 * @brief Set up the log files of a run.
 *
 * A run writes its log files only when it asks for a level: a run without the
 * level, with `off` or with a name the launcher does not know creates neither a
 * directory nor a file, and the sandbox of every process it starts writes no
 * log either, because the run hands it no log directory.
 *
 * A run which asks for a level writes the log of the launcher itself into the
 * log directory of the run, next to the log file of every process of its
 * sandbox.
 *
 * A log which cannot be created never fails the run: the failure is reported
 * and the run continues without that log file.
 *
 * @param[in,out] opt The options of the run. The log directory is replaced by
 *                    its resolved absolute path, and it is cleared when the run
 *                    writes no log file at all.
 */
void SetupLogging(appbox::CommandLineOptions& opt)
{
    const auto level = appbox::UTF8ToWide(opt.log_level);

    /*
     * An unknown name is reported by SetLogLevel(), which keeps the report of a
     * name which is not a level visible instead of muting the run silently.
     */
    appbox::SetLogLevel(level);

    if (!appbox::IsLogLevelEnabled(level))
    {
        /*
         * The run asks for no log file: no directory is created and the
         * messages of the launcher are dropped as well, so a run without a log
         * costs nothing.
         */
        appbox::SetLogLevel(L"off");
        opt.log_dir.clear();
        return;
    }

    opt.log_dir = ResolveLogDir(opt.log_dir);

    const std::filesystem::path dir(opt.log_dir);
    std::error_code             ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        SPDLOG_ERROR("the log directory '{}' cannot be created: {}", appbox::WideToUTF8(opt.log_dir), ec.message());
        return;
    }

    try
    {
        const auto path = (dir / OwnLogFileName()).wstring();
        auto       logger = spdlog::basic_logger_mt("Launcher", path.c_str());
        /*
         * The log of a run which crashes is worth as much as its tail, so every
         * message is flushed instead of waiting for the buffer to fill.
         */
        logger->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(logger);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("the log file of the run cannot be opened: {}", e.what());
        return;
    }

    /* The level applies to the file logger the run just installed as well. */
    appbox::SetLogLevel(level);
}

} // namespace

static void SetupConfigFile(appbox::CommandLineOptions& opt, const std::wstring& arg)
{
    std::filesystem::path path(arg);
    opt.config_dir = std::filesystem::path(arg).parent_path().wstring();

    std::ifstream f(path);
    opt.override_config = nlohmann::json::parse(f);
}

/**
 * @brief Start the target of the process launcher and exit with its exit code.
 * @param[in] app The command line, whose first remaining argument is the
 *                target and whose other remaining arguments belong to it.
 * @param[in] hide_console True to start the target without a console window.
 */
static void RunAsStarter(CLI::App& app, bool hide_console)
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

    exit(appbox::ProcessLauncher(exe_path, exe_args, hide_console));
}

appbox::CommandLineOptions::CommandLineOptions()
{
    wargc = 0;
    wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    is_process_launcher = false;
    hide_console = false;
    has_startup_trigger = false;
    shell = false;
    log_level = "off";
}

appbox::CommandLineOptions::~CommandLineOptions()
{
    LocalFree(wargv);
}

bool appbox::CommandLineOptions::ParseOptions()
{
    CLI::App app;
    app.prefix_command();
    app.add_option_function<std::wstring>(
        "--X-AppBox-LogLevel",
        [this](const std::wstring& level) {
            /*
             * The level describes the whole run: the launcher reports it and the
             * sandbox of every process it starts reports it as well, so a run
             * which asks for `info` never writes the trace of every kernel
             * call of the sandboxed application. A run which names no level
             * writes no log file at all, which is what a run does by default.
             */
            this->log_level = appbox::WideToUTF8(level);
        },
        "Level of the log files of the run: trace, debug, info, warn, err, critical or off. A run which names no "
        "level writes no log file. Default: no log file");
    app.add_option_function<std::wstring>(
        "--X-AppBox-LogDir",
        [this](const std::wstring& dir) {
            /*
             * The directory is resolved once the whole command line was read:
             * the level decides whether the run writes a log file at all, and
             * the two options may appear in either order.
             */
            this->log_dir = dir;
        },
        "Directory of the log files of the run. A relative path is resolved against the current directory. "
        "Default: the log directory beside the launcher");
    app.add_option_function<std::wstring>(
        "--X-AppBox-ConfigFile", [this](const std::wstring& arg) { SetupConfigFile(*this, arg); },
        "Use config file instead of loading builtin config file");
    app.add_option("--X-AppBox-ProcessLauncher", is_process_launcher, "Run as the process launcher of the launcher");
    app.add_option("--X-AppBox-HideConsole", hide_console, "Start the target without a console window");
    /*
     * The shell takes the command it runs from the remaining arguments, so the
     * option itself carries no value and every option of the launcher has to
     * precede it on the command line.
     */
    app.add_flag("--X-AppBox-Shell", shell, "Run the shell of the host inside the sandbox");
    app.add_option_function<std::wstring>(
        "--X-AppBox-Startup",
        [this](const std::wstring& arg) {
            startup_trigger = appbox::WideToUTF8(arg);
            has_startup_trigger = true;
        },
        "Run the startup file with the given trigger instead of the auto start files");

    CLI11_PARSE(app, wargc, wargv);

    SetupLogging(*this);

    if (is_process_launcher)
    {
        RunAsStarter(app, hide_console);
        return false;
    }

    for (auto& arg : app.remaining())
    {
        extra_args.push_back(arg);
    }

    return true;
}
