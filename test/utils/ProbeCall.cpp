#include "RemoteAccepter.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <spdlog/spdlog.h>
#include <CLI/Encoding.hpp>
#include "launcher/Config.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include "BuildCommandLine.hpp"
#include "ProbeCall.hpp"
#include "MultiInstance.hpp"
#include "ProbePath.hpp"
#include "Test.hpp"
#include "TestSandboxId.hpp"
#include "probe/Bind.hpp"
#include "probe/Exit.hpp"
#include "probe/StartupStarted.hpp"

namespace
{

/** Budget of the exit request of a probe, in milliseconds. */
constexpr std::uint32_t kProbeExitTimeoutMs = 5000;

/** Budget of a run of the shell, in milliseconds. */
constexpr std::uint32_t kShellTimeoutMs = 60000;

/** Delay between two polls of the state of a run, in milliseconds. */
constexpr std::uint32_t kPollDelayMs = 5;

/**
 * @brief Budget of the end of one launcher run, in milliseconds.
 *
 * The launcher returns when its last program left, so a run which does not end
 * means a probe which never left: the budget turns that into a failure of the
 * case instead of a wait which hides the defect.
 */
constexpr std::uint32_t kProbeRunTimeoutMs = 120000;

/**
 * @brief Budget of a case which waits for the programs of a request to report.
 *
 * The programs of a request are created by the master instance before it
 * answers the request, so they need the time of a process start to reach the
 * probe server of the case.
 */
constexpr std::uint32_t kSecondaryReportTimeoutMs = 10000;

/**
 * @brief The probes of a case and the runs which own them.
 */
struct ProbeServer
{
    typedef std::shared_ptr<ProbeServer> Ptr;

    std::string                 pipe_path; /* Pipe path */
    appbox::RemoteAccepter::Ptr accepter;  /* Accepter of the probes */
    std::atomic_uint64_t        key_id;    /* Key generator */

    std::mutex mutex; /* Mutex of the owners */

    /**
     * @brief Binder of every live run, by key.
     *
     * A run registers itself before it starts its launcher, so the first probe
     * which connects finds it. The binder returns false when the run is gone or
     * is ending, which tells the server to end the process instead of keeping
     * it alive for a handle nobody holds any more.
     */
    std::map<std::string, std::function<bool(const appbox::RemoteSession::Ptr&)>> owners;
};

/**
 * @brief Global probe server context.
 */
ProbeServer::Ptr s_probe_srv;

/**
 * @brief Bind one probe handler to the session it is registered on.
 *
 * A probe session answers its own requests, so the handler needs the session it
 * serves. The handler is stored inside the session and therefore holds it
 * weakly: a strong reference would keep every connection of a case alive.
 *
 * @param[in] session Session the handler is registered on.
 * @param[in] handler Implementation of the probe method.
 * @return The handler to register.
 */
appbox::RemoteSession::MethodHandler BindSession(const appbox::RemoteSession::Ptr& session,
                                                 void (*handler)(const appbox::RemoteSession::Ptr&, uint64_t,
                                                                 const nlohmann::json&))
{
    std::weak_ptr<appbox::RemoteSession> weak_session = session;

    return [weak_session, handler](uint64_t id, const nlohmann::json& param) {
        if (auto bound = weak_session.lock())
        {
            handler(bound, id, param);
        }
    };
}

/**
 * @brief Attach the session of a probe process to the run which started it.
 *
 * A probe sends this request before it answers anything, so the run knows which
 * sessions belong to it. A process whose run is gone is ended at once, because
 * a probe which kept serving would keep the launcher of its run alive and the
 * case would wait for the end of a run nobody owns any more.
 *
 * @param[in] session Session of the probe process.
 * @param[in] id Id of the request.
 * @param[in] req Parameters of the request.
 */
void OnProbeBind(const appbox::RemoteSession::Ptr& session, uint64_t id, const nlohmann::json& req)
{
    appbox::test::ProtocolBind::Req bind;
    try
    {
        bind = req;
    }
    catch (const nlohmann::json::exception& e)
    {
        SPDLOG_ERROR("the parameters of {} cannot be read: {}", appbox::test::ProtocolBind::Method, e.what());
        session->SendResponse(
            id, tl::unexpected<appbox::RemoteError>(appbox::RemoteError{ -32602, "Invalid params", nlohmann::json() }));
        return;
    }

    std::function<bool(const appbox::RemoteSession::Ptr&)> owner;
    {
        std::lock_guard<std::mutex> lock(s_probe_srv->mutex);
        auto                        it = s_probe_srv->owners.find(bind.key);
        if (it != s_probe_srv->owners.end())
        {
            owner = it->second;
        }
    }

    if (!owner || !owner(session))
    {
        SPDLOG_WARN("the probe of the key '{}' has no run any more, it is ended", bind.key);
        appbox::test::ProtocolExit::Req exit_request;
        session->Call(appbox::test::ProtocolExit::Method, nlohmann::json(exit_request));
    }

    session->SendResponse(id, nlohmann::json(appbox::test::ProtocolBind::Rsp{}));
}

/**
 * @brief Create the probe server of the case.
 */
void InitProbeServer()
{
    s_probe_srv = std::make_shared<ProbeServer>();

    {
        std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto        random_str = appbox::RandomString(16);
        s_probe_srv->pipe_path = fmt::format(R"(\\.\pipe\appbox-test-{}-{})", timestamp, random_str);
    }

    s_probe_srv->key_id = 0;
    s_probe_srv->accepter = appbox::RemoteAccepter::Create(s_probe_srv->pipe_path);

    /*
     * Every probe process connects with a session of its own, so the bind is
     * registered on each of them: the answer of a request belongs to the
     * session which received it.
     */
    s_probe_srv->accepter->SetSessionCallback([](const appbox::RemoteSession::Ptr& session) {
        session->RegisterMethod(appbox::test::ProtocolBind::Method, BindSession(session, OnProbeBind));
    });

    s_probe_srv->accepter->Start();
}

/**
 * @brief Get the probe server of the case, which is created on the first use.
 *
 * Every helper of a case reports over the same pipe, so the server is created
 * once for the whole process: a helper which created a server of its own would
 * replace the server the probes of an earlier call already connect to, and
 * those probes would never reach it.
 *
 * @return The server.
 */
ProbeServer::Ptr EnsureProbeServer()
{
    static std::once_flag once;
    std::call_once(once, InitProbeServer);
    return s_probe_srv;
}

/**
 * @brief Generate a temporary key by process id and global counter.
 * @return The key of a new run.
 */
std::string GenTempKey()
{
    auto pid = GetCurrentProcessId();
    return fmt::format("{}-{}", pid, s_probe_srv->key_id++);
}

/**
 * @brief Report the log files the processes of a case wrote.
 *
 * The launcher writes a log file of its own into the log directory of the case
 * and every process the sandbox is injected into writes one as well, named
 * after the program, the UTC time it started at and its process id. A run which
 * fails names those files, so a reader of the artifact of a failed run knows
 * which file belongs to which process without guessing.
 *
 * @param[in] cwd Directory of the case.
 */
void ReportSandboxLogs(const std::wstring& cwd)
{
    std::error_code                    ec;
    std::vector<std::filesystem::path> logs;

    const auto dir = std::filesystem::path(cwd) / appbox::test::kCaseLogDirName;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == L".log")
        {
            logs.push_back(entry.path());
        }
    }

    std::sort(logs.begin(), logs.end());
    for (const auto& log : logs)
    {
        SPDLOG_ERROR("log of the run: {}", appbox::WideToUTF8(log.filename().wstring()));
    }
}

/**
 * @brief Adapt the configuration of a case to the probe process.
 *
 * A case which describes no startup file starts the probe once, like the single
 * main program of a packaged application, and every startup file is pointed at
 * the probe executable of the case instead of the program of the case.
 *
 * The console window of the probe is hidden as well: the probe is a console
 * program, while the launcher is a GUI program without a console, so Windows
 * would open a console window for every probe process of every case and that
 * window would pop up on the desktop of the machine the cases run on.
 *
 * @param[in] config Configuration of the case.
 * @param[in] probe_path Path of the probe executable the case asked for.
 * @param[in] override_executable Whether every startup file points at the probe
 *                    executable. A case which describes the image of a startup
 *                    file itself clears the flag, so the path the case writes
 *                    is the path the launcher resolves.
 * @return The configuration of the probe run.
 */
appbox::LauncherConfig OverrideConfig(const appbox::LauncherConfig& config, const std::wstring& probe_path,
                                      bool override_executable = true)
{
    appbox::LauncherConfig copy_config = config;

    if (copy_config.startups.empty())
    {
        /*
         * A configuration of a case which does not describe startup files
         * starts the probe once, like the single main program of the packaged
         * application.
         */
        appbox::LauncherStartup startup;
        startup.trigger = "probe";
        startup.auto_start = true;
        copy_config.startups.push_back(std::move(startup));
    }

    /*
     * The probe runs instead of the packaged application, so every startup file
     * points at the probe executable of the bit size the case asked for.
     */
    if (override_executable)
    {
        for (auto& startup : copy_config.startups)
        {
            startup.executable = appbox::WideToUTF8(probe_path);
        }
    }

    copy_config.hide_console = true;

    /*
     * The startup screen of a run is turned off as well: the cases run on the
     * desktop of the machine, and a window which pops up for every case would
     * cover the work of the user. A case which checks the screen turns it on
     * again, see ProbeStartupRun().
     */
    copy_config.startup.screen = false;

    return copy_config;
}

/**
 * @brief Run the launcher of a configuration document and wait for it.
 *
 * The document is written beside the working directory of the case, which is
 * also the directory the launcher resolves its sandbox layout against.
 *
 * @param[in] cwd The current working directory.
 * @param[in] config The launcher configuration document.
 * @param[in] args Arguments of the launcher, without the options which select
 *                 and log the configuration.
 * @param[in] log The log options of the run, see LogRun.
 * @param[out] launcher_pid Process id of the launcher which was started, may be
 *                          null.
 * @param[in] executable_path Path of the launcher program to run, empty to use
 *                            the launcher of the test configuration.
 * @param[in] pass_config_file Whether the run names the configuration file
 *                             with `--X-AppBox-ConfigFile`. A case which runs
 *                             a launcher with an embedded configuration clears
 *                             the flag.
 * @param[in] config_file_name Name of the file the configuration is written
 *                             into. Two launchers of one sandbox share the
 *                             directory of the sandbox, so the second instance
 *                             of a case writes its configuration beside the
 *                             file of the master instance.
 * @param[in] working_directory Directory the launcher works in, empty to
 *                             inherit the directory of the test process.
 * @return The exit code of the launcher.
 */
DWORD RunLauncherJson(const std::wstring& cwd, const nlohmann::json& config, const std::vector<std::wstring>& args,
                      const appbox::test::LogRun& log, std::uint32_t* launcher_pid = nullptr,
                      const std::wstring& executable_path = std::wstring(), bool pass_config_file = true,
                      const std::wstring& config_file_name = L"config.json",
                      const std::wstring& working_directory = std::wstring())
{
    const auto& launcher_path = executable_path.empty() ? appbox::test::config.launcher_path : executable_path;

    std::vector<std::wstring> full_args;
    if (pass_config_file)
    {
        std::filesystem::path cfg_path;
        {
            auto data = config.dump(2);

            cfg_path = std::filesystem::path(cwd) / config_file_name;
            std::ofstream ofs(cfg_path, std::ios::binary | std::ios::trunc);
            ofs.write(data.data(), data.size());
        }

        full_args.push_back(L"--X-AppBox-ConfigFile");
        full_args.push_back(cfg_path.wstring());
    }

    /*
     * A case asks for the log files of its run by default, which keeps them
     * inside the working directory of the case instead of the directory of the
     * launcher program. A case which checks the log options of the launcher
     * itself leaves an option out or changes its value.
     */
    if (log.pass_level)
    {
        full_args.push_back(L"--X-AppBox-LogLevel");
        full_args.push_back(log.level.empty() ? appbox::test::config.log_level : log.level);
    }

    if (log.pass_dir)
    {
        full_args.push_back(L"--X-AppBox-LogDir");
        full_args.push_back(log.dir.empty() ? (std::filesystem::path(cwd) / appbox::test::kCaseLogDirName).wstring()
                                            : log.dir);
    }

    full_args.insert(full_args.end(), args.begin(), args.end());

    auto cmd = appbox::BuildCommandLine(launcher_path, full_args);

    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));

    /*
     * The directory of the launcher is the directory of the run: the programs
     * of the run inherit it, so a case which checks the working directory of a
     * request names the directory the launcher has to work in.
     */
    const LPCWSTR current_directory = working_directory.empty() ? nullptr : working_directory.c_str();

    if (!CreateProcessW(launcher_path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, current_directory,
                        &startup_info, &process_info))
    {
        auto msg = fmt::format("CreateProcessW failed: {}", GetLastError());
        SPDLOG_ERROR(msg);
        throw std::runtime_error(msg);
    }

    if (launcher_pid != nullptr)
    {
        *launcher_pid = process_info.dwProcessId;
    }

    CloseHandle(process_info.hThread);

    auto wait_ret = WaitForSingleObject(process_info.hProcess, INFINITE);
    if (wait_ret != WAIT_OBJECT_0)
    {
        auto msg = fmt::format("WaitForSingleObject failed: {}", wait_ret);
        SPDLOG_ERROR(msg);
        throw std::runtime_error(msg);
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process_info.hProcess, &exit_code))
    {
        auto msg = fmt::format("GetExitCodeProcess failed: {}", GetLastError());
        SPDLOG_ERROR(msg);
        throw std::runtime_error(msg);
    }

    CloseHandle(process_info.hProcess);
    return exit_code;
}

/**
 * @brief Run the launcher of a configuration and wait for it.
 *
 * The identifier of the sandbox is a required member of the configuration and
 * the launcher derives the name of its control pipe from it, so a case which
 * describes none gets the identifier of its own working directory, see
 * `TestSandboxId.hpp`: every case then runs its own sandbox, and a case which
 * checks the identifier names one itself.
 *
 * @param[in] cwd The current working directory.
 * @param[in] config The launcher configuration.
 * @param[in] args Arguments of the launcher, without the options which select
 *                 and log the configuration.
 * @param[in] log The log options of the run, see LogRun.
 * @param[out] launcher_pid Process id of the launcher which was started, may be
 *                          null.
 * @param[in] executable_path Path of the launcher program to run, empty to use
 *                            the launcher of the test configuration.
 * @param[in] pass_config_file Whether the run names the configuration file
 *                             with `--X-AppBox-ConfigFile`. A case which runs
 *                             a launcher with an embedded configuration clears
 *                             the flag.
 * @param[in] config_file_name Name of the file the configuration is written
 *                             into, see RunLauncherJson().
 * @param[in] working_directory Directory the launcher works in, empty to
 *                             inherit the directory of the test process.
 * @return The exit code of the launcher.
 */
DWORD RunLauncher(const std::wstring& cwd, const appbox::LauncherConfig& config, const std::vector<std::wstring>& args,
                  const appbox::test::LogRun& log, std::uint32_t* launcher_pid = nullptr,
                  const std::wstring& executable_path = std::wstring(), bool pass_config_file = true,
                  const std::wstring& config_file_name = L"config.json",
                  const std::wstring& working_directory = std::wstring())
{
    nlohmann::json document = config;

    const auto id = document.find("sandbox_id");
    if (id == document.end() || !id->is_string() || id->get<std::string>().empty())
    {
        document["sandbox_id"] = appbox::test::SandboxIdForPath(cwd);
    }

    return RunLauncherJson(cwd, document, args, log, launcher_pid, executable_path, pass_config_file, config_file_name,
                           working_directory);
}

/**
 * @brief Build the arguments which make a launcher start the probe of a run.
 *
 * @param[in] key Probe key of the run, which the probe reports to.
 * @param[in] trigger Trigger for `--X-AppBox-Startup`, empty to omit the option
 *                    so the launcher starts the auto start files.
 * @param[in] marker Marker of the probe processes, empty to omit it.
 * @return The arguments of the launcher.
 */
std::vector<std::wstring> ProbeArgs(const std::string& key, const std::string& trigger,
                                    const std::string& marker = std::string())
{
    std::vector<std::wstring> args;
    if (!trigger.empty())
    {
        args.push_back(L"--X-AppBox-Startup");
        args.push_back(CLI::widen(trigger));
    }

    /*
     * The marker is a global option of the probe, so it has to precede the
     * probe subcommand: the launcher appends these arguments to the arguments
     * of every startup file, and the command line parser of the probe reads a
     * parent option before it enters the subcommand.
     *
     * The option is a marker of its own and not the marker of the startup file:
     * the arguments of a request follow the arguments of the startup file,
     * which already carries `--startup_marker`, and the parser of the probe
     * refuses an option which is given twice.
     */
    if (!marker.empty())
    {
        args.push_back(L"--probe_marker");
        args.push_back(CLI::widen(marker));
    }

    args.push_back(L"probe");
    args.push_back(L"--probe_pipe");
    args.push_back(CLI::widen(s_probe_srv->pipe_path));
    args.push_back(L"--probe_key");
    args.push_back(CLI::widen(key));
    return args;
}

/**
 * @brief Run the launcher of a configuration so it starts the probes of a run.
 *
 * @param[in] key Probe key of the run.
 * @param[in] cwd The current working directory.
 * @param[in] config The launcher configuration, already pointed at the probe.
 * @param[in] trigger Trigger for `--X-AppBox-Startup`, empty to omit the option.
 * @param[in] log The log options of the run, see LogRun.
 * @param[out] launcher_pid Process id of the launcher which was started, may be
 *                          null.
 * @param[in] launcher_path Program of the run, empty to run the launcher of the
 *                          test configuration.
 * @param[in] working_directory Directory the launcher works in, empty to
 *                          inherit the directory of the test process.
 * @return The exit code of the launcher.
 */
DWORD RunLauncherForProbe(const std::string& key, const std::wstring& cwd, const appbox::LauncherConfig& config,
                          const std::string& trigger, const appbox::test::LogRun& log,
                          std::uint32_t* launcher_pid = nullptr, const std::wstring& launcher_path = std::wstring(),
                          const std::wstring& working_directory = std::wstring())
{
    return RunLauncher(cwd, config, ProbeArgs(key, trigger), log, launcher_pid, launcher_path, true, L"config.json",
                       working_directory);
}

/**
 * @brief Build the runner which starts the launcher of a configuration.
 *
 * The runner captures the configuration and the layout of the run by value, so
 * the handle it belongs to owns everything the thread of the launcher reads.
 *
 * @param[in] cwd The current working directory.
 * @param[in] launcher_config The launcher configuration.
 * @param[in] trigger Trigger for `--X-AppBox-Startup`, empty to omit the option.
 * @param[in] startup_screen Whether the run shows the startup screen.
 * @param[in] log The log options of the run, see LogRun.
 * @param[in] use_configured_executable Whether every startup file keeps the
 *                    executable the configuration spells.
 * @param[in] launcher_path Program of the run, empty to run the launcher of the
 *                    test configuration.
 * @param[in] working_directory Directory the launcher works in, empty to
 *                    inherit the directory of the test process.
 * @return The runner of the launcher.
 */
std::function<std::uint32_t(const std::string&, std::uint32_t*)> MakeLauncherRunner(
    const std::wstring& cwd, const appbox::LauncherConfig& launcher_config, const std::string& trigger,
    bool startup_screen, const appbox::test::LogRun& log, bool use_configured_executable,
    const std::wstring& launcher_path, const std::wstring& working_directory)
{
    auto copy_config = OverrideConfig(launcher_config, appbox::test::ProbePath(appbox::test::ProbeBitness::Bit64),
                                      !use_configured_executable);
    copy_config.startup.screen = startup_screen;

    return [cwd, copy_config, trigger, log, launcher_path, working_directory](const std::string& key,
                                                                              std::uint32_t*     launcher_pid) {
        return static_cast<std::uint32_t>(
            RunLauncherForProbe(key, cwd, copy_config, trigger, log, launcher_pid, launcher_path, working_directory));
    };
}

/**
 * @brief Build the request of a probe which reports the startup file of its
 *        process.
 * @param[in] probe_data Task data of the probe, empty for a plain report.
 * @return The request of the probe.
 */
appbox::test::ProtocolStartupStarted::Req StartupRequest(const nlohmann::json& probe_data)
{
    if (!probe_data.is_object() || probe_data.empty())
    {
        return appbox::test::ProtocolStartupStarted::Req{};
    }

    return probe_data.get<appbox::test::ProtocolStartupStarted::Req>();
}

/**
 * @brief Fill the report of one startup file from the answer of its probe.
 * @param[in] rsp Answer of the probe process of the startup file.
 * @return The report of the startup file.
 */
appbox::test::StartupReport MakeStartupReport(const appbox::test::ProtocolStartupStarted::Rsp& rsp)
{
    appbox::test::StartupReport report;
    report.marker = rsp.marker;
    report.pid = rsp.pid;
    report.parent_pid = rsp.parent_pid;
    report.module_path = rsp.module_path;
    report.command_line = rsp.command_line;
    report.environment = rsp.environment;
    report.cwd = rsp.cwd;

    for (const auto& operation : rsp.relative_ops)
    {
        appbox::test::StartupReport::RelativeOperation outcome;
        outcome.code = operation.code;
        outcome.data = operation.data;
        report.relative_ops.push_back(std::move(outcome));
    }

    return report;
}

/**
 * @brief Fill the reports of a run from the answers of its probes.
 * @param[in,out] run Run to fill.
 * @param[in] answers Answers of the probes of the run.
 */
void FillStartupRun(appbox::test::StartupRun&                                     run,
                    const std::vector<appbox::test::ProtocolStartupStarted::Rsp>& answers)
{
    for (const auto& answer : answers)
    {
        run.started.push_back(answer.marker);
        run.reports.push_back(MakeStartupReport(answer));
    }

    std::sort(run.started.begin(), run.started.end());
}

} // namespace

/**
 * @brief Internal state of one launcher run.
 */
struct appbox::test::ProbeRun::Data
{
    std::wstring                                     cwd;      /* Directory of the case */
    std::string                                      key;      /* Key of the run */
    std::function<std::uint32_t(const std::string&)> launcher; /* Runner of the launcher */
    std::thread                                      worker;   /* Thread the launcher runs on */

    mutable std::mutex                      mutex;    /* Mutex of the sessions */
    std::vector<appbox::RemoteSession::Ptr> sessions; /* Sessions of the probes of the run */

    std::atomic_bool     finished{ false }; /* Whether the launcher left */
    std::atomic_bool     stopping{ false }; /* Whether the run ends its probes */
    std::atomic_bool     exited{ false };   /* Whether the probes were asked to leave */
    std::uint64_t        start_tick = 0;    /* Tick the launcher was started at */
    std::atomic_uint64_t finish_tick{ 0 };  /* Tick the launcher left at */
    std::uint32_t        exit_code = 0;     /* Exit code of the launcher */
    std::exception_ptr   error;             /* Failure of the launcher */
};

appbox::test::ProbeRun::ProbeRun(const std::wstring& cwd, std::function<std::uint32_t(const std::string&)> launcher)
    : data_(std::make_shared<Data>())
{
    EnsureProbeServer();

    data_->cwd = cwd;
    data_->launcher = std::move(launcher);
    data_->key = GenTempKey();

    /*
     * The run is registered before its launcher starts: a probe which connects
     * first must find the run, otherwise the server ends it.
     */
    std::weak_ptr<Data> weak = data_;
    {
        std::lock_guard<std::mutex> lock(s_probe_srv->mutex);
        s_probe_srv->owners[data_->key] = [weak](const appbox::RemoteSession::Ptr& session) {
            auto run = weak.lock();
            if (!run || run->stopping)
            {
                return false;
            }

            SPDLOG_INFO("a probe of the run '{}' connected", run->key);

            std::lock_guard<std::mutex> session_lock(run->mutex);
            run->sessions.push_back(session);
            return true;
        };
    }

    /*
     * The launcher returns only when its last program left, so it runs on a
     * worker thread: the caller decides when the probes of the run leave.
     */
    data_->start_tick = GetTickCount64();
    data_->worker = std::thread([data = data_]() {
        try
        {
            data->exit_code = data->launcher(data->key);
        }
        catch (...)
        {
            data->error = std::current_exception();
            data->exit_code = 1;
        }

        /*
         * The end of the launcher is reported after the tick it ended at, so a
         * caller which sees the end reads the tick of the same moment.
         */
        data->finish_tick = GetTickCount64();
        data->finished = true;
    });
}

appbox::test::ProbeRun::~ProbeRun()
{
    try
    {
        ExitAll(0);
        Wait();
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("the run of '{}' failed while it ended: {}", data_->key, e.what());
    }

    std::lock_guard<std::mutex> lock(s_probe_srv->mutex);
    s_probe_srv->owners.erase(data_->key);
}

const std::string& appbox::test::ProbeRun::Key() const
{
    return data_->key;
}

std::size_t appbox::test::ProbeRun::SessionCount() const
{
    std::lock_guard<std::mutex> lock(data_->mutex);
    return data_->sessions.size();
}

bool appbox::test::ProbeRun::WaitForSessions(std::uint32_t quiet_ms, std::uint32_t timeout_ms, bool wait_for_launcher)
{
    const auto  deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    auto        quiet = std::chrono::steady_clock::now() + std::chrono::milliseconds(quiet_ms);
    std::size_t last = 0;

    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(data_->mutex);
            if (data_->sessions.size() != last)
            {
                last = data_->sessions.size();
                quiet = std::chrono::steady_clock::now() + std::chrono::milliseconds(quiet_ms);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (last > 0 && now >= quiet)
        {
            return true;
        }

        if (data_->finished)
        {
            /*
             * A launcher which left started no program, or started the programs
             * of another launcher: the set of probes cannot grow any more.
             */
            if (data_->exit_code != 0 || wait_for_launcher)
            {
                return last > 0;
            }
        }

        if (now >= deadline)
        {
            if (last == 0)
            {
                SPDLOG_WARN("no probe connected for the run '{}' in {} ms", data_->key, timeout_ms);
            }

            return last > 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }
}

void appbox::test::ProbeRun::ExitAll(std::int32_t code)
{
    data_->stopping = true;

    /*
     * The call is idempotent: a probe which already left answers nothing, and
     * a second request would wait for its whole budget before the case fails
     * for no reason.
     */
    if (data_->exited.exchange(true))
    {
        return;
    }

    /*
     * A launcher which left does not mean that its probes left: the programs of
     * a request are created by another launcher and outlive the instance which
     * asked for them, so the sessions are ended whether the launcher runs or
     * not.
     */
    std::vector<appbox::RemoteSession::Ptr> sessions;
    {
        std::lock_guard<std::mutex> lock(data_->mutex);
        sessions = data_->sessions;
    }

    appbox::test::ProtocolExit::Req request;
    request.code = code;

    for (const auto& session : sessions)
    {
        const auto answer = session->Call<appbox::test::ProtocolExit>(request, kProbeExitTimeoutMs);
        if (!answer.has_value())
        {
            SPDLOG_WARN("the exit request of a probe failed: {}", answer.error().message);
        }
    }
}

bool appbox::test::ProbeRun::Finished() const
{
    return data_->finished;
}

std::uint32_t appbox::test::ProbeRun::Wait()
{
    /*
     * The wait is bounded: a probe which does not leave keeps its launcher
     * alive, and a case which waited for ever would hide the defect behind the
     * timeout of the test executable.
     */
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kProbeRunTimeoutMs);
    while (!data_->finished && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }

    if (!data_->finished)
    {
        SPDLOG_ERROR("the run '{}' did not end: {} probe(s) connected", data_->key, SessionCount());
        ReportSandboxLogs(data_->cwd);
        throw std::runtime_error(fmt::format("the launcher run '{}' did not end", data_->key));
    }

    if (data_->worker.joinable())
    {
        data_->worker.join();
    }

    if (data_->error)
    {
        std::rethrow_exception(data_->error);
    }

    return data_->exit_code;
}

bool appbox::test::ProbeRun::WaitForLauncher(std::uint32_t timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!data_->finished && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }

    if (!data_->finished)
    {
        SPDLOG_WARN("the launcher of the run '{}' did not end in {} ms", data_->key, timeout_ms);
    }

    return data_->finished;
}

std::uint32_t appbox::test::ProbeRun::ExitCode() const
{
    return data_->exit_code;
}

std::uint32_t appbox::test::ProbeRun::LauncherLifetimeMs() const
{
    const auto finish = data_->finish_tick.load();
    if (finish == 0 || finish < data_->start_tick)
    {
        return 0;
    }

    return static_cast<std::uint32_t>(finish - data_->start_tick);
}

std::uint32_t appbox::test::ProbeRun::Pid() const
{
    std::lock_guard<std::mutex> lock(data_->mutex);
    if (data_->sessions.empty())
    {
        return 0;
    }

    return data_->sessions.front()->PeerProcessId();
}

nlohmann::json appbox::test::ProbeRun::CallRaw(const std::string& method, const nlohmann::json& params,
                                               std::uint32_t timeout_ms)
{
    if (!WaitForSessions(0))
    {
        ReportSandboxLogs(data_->cwd);
        throw std::runtime_error(fmt::format("no probe connected for '{}'", method));
    }

    appbox::RemoteSession::Ptr session;
    {
        std::lock_guard<std::mutex> lock(data_->mutex);
        session = data_->sessions.front();
    }

    const auto answer = session->Call(method, params, timeout_ms);
    if (!answer.has_value())
    {
        ReportSandboxLogs(data_->cwd);
        throw std::runtime_error(fmt::format("the probe failed '{}': {}", method, answer.error().message));
    }

    return answer.value();
}

std::vector<nlohmann::json> appbox::test::ProbeRun::CallAllRaw(const std::string& method, const nlohmann::json& params,
                                                               std::uint32_t timeout_ms)
{
    if (!WaitForSessions())
    {
        ReportSandboxLogs(data_->cwd);
        throw std::runtime_error(fmt::format("no probe connected for '{}'", method));
    }

    std::vector<appbox::RemoteSession::Ptr> sessions;
    {
        std::lock_guard<std::mutex> lock(data_->mutex);
        sessions = data_->sessions;
    }

    std::vector<nlohmann::json> answers;
    for (const auto& session : sessions)
    {
        const auto answer = session->Call(method, params, timeout_ms);
        if (!answer.has_value())
        {
            /*
             * A program of the run may be gone: a master instance terminates the
             * programs of a request it has to roll back, so a run reports what
             * its programs reported and a case which needs every answer checks
             * the number of the reports itself.
             */
            SPDLOG_WARN("the probe of the run '{}' did not answer '{}': {}", data_->key, method,
                        answer.error().message);
            continue;
        }

        answers.push_back(answer.value());
    }

    return answers;
}

appbox::test::Probe::Probe(const std::wstring& cwd, const LauncherConfig& launcher_config, ProbeBitness bitness,
                           const LogRun& log, const std::string& marker, const std::vector<std::wstring>& args)
{
    /*
     * The probe is a program of its own, so the path of the executable is the
     * one the run named for the bit size: a case which asks for a probe the run
     * does not know cannot fall back to another one, because the operations of
     * a case would then run in a process of the wrong bit size.
     */
    const std::wstring& probe_path = ProbePath(bitness);
    std::error_code     ec;
    if (probe_path.empty() || !std::filesystem::is_regular_file(probe_path, ec))
    {
        const auto msg = fmt::format("the {} bit probe is unavailable: pass --probe32= and --probe64=",
                                     bitness == ProbeBitness::Bit32 ? 32 : 64);
        SPDLOG_ERROR(msg);
        throw std::runtime_error(msg);
    }

    auto copy_config = OverrideConfig(launcher_config, probe_path);

    run_ = std::make_unique<ProbeRun>(cwd, [cwd, copy_config, log, marker, args](const std::string& key) {
        auto full_args = ProbeArgs(key, std::string(), marker);
        full_args.insert(full_args.end(), args.begin(), args.end());
        return static_cast<std::uint32_t>(RunLauncher(cwd, copy_config, full_args, log));
    });
}

appbox::test::Probe::~Probe()
{
    try
    {
        Exit(0);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("the probe of the run failed while it ended: {}", e.what());
    }
}

void appbox::test::Probe::Exit(std::int32_t code)
{
    if (run_)
    {
        run_->ExitAll(code);
        run_->Wait();
    }
}

std::uint32_t appbox::test::Probe::Wait()
{
    return run_ ? run_->Wait() : 0;
}

std::uint32_t appbox::test::Probe::Pid() const
{
    return run_ ? run_->Pid() : 0;
}

appbox::test::ProbeRun& appbox::test::Probe::Run()
{
    return *run_;
}

appbox::test::StartupRunHandle::StartupRunHandle(const std::wstring& cwd, LauncherRunner launcher)
{
    /*
     * The launcher thread writes the process id of the launcher into the
     * reports of the handle, so it holds those reports by pointer. The member
     * which owns the run is declared after the reports, which ends the run
     * before the reports go away.
     */
    StartupRun* reports = &run_;

    probe_run_ = std::make_unique<ProbeRun>(cwd, [launcher = std::move(launcher), reports](const std::string& key) {
        return launcher(key, &reports->launcher_pid);
    });
}

appbox::test::StartupRunHandle::StartupRunHandle(const std::wstring& cwd, const appbox::LauncherConfig& launcher_config,
                                                 const std::string& trigger, bool startup_screen,
                                                 const appbox::test::LogRun& log, bool use_configured_executable,
                                                 const std::wstring&   launcher_path,
                                                 const std::wstring&   working_directory,
                                                 const nlohmann::json& probe_data)
    : StartupRunHandle(cwd, MakeLauncherRunner(cwd, launcher_config, trigger, startup_screen, log,
                                               use_configured_executable, launcher_path, working_directory))
{
    probe_data_ = probe_data;
}

appbox::test::StartupRunHandle::StartupRunHandle(const std::wstring& cwd, const std::wstring& launcher_path,
                                                 const std::string& trigger, const std::string& marker,
                                                 const appbox::test::LogRun& log)
    : StartupRunHandle(cwd, [cwd, launcher_path, trigger, marker, log](const std::string& key,
                                                                       std::uint32_t*     launcher_pid) {
          return static_cast<std::uint32_t>(RunLauncher(cwd, appbox::LauncherConfig{}, ProbeArgs(key, trigger, marker),
                                                        log, launcher_pid, launcher_path, false));
      })
{
}

appbox::test::StartupRunHandle::~StartupRunHandle()
{
    try
    {
        Exit(0);
        Wait();
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("the run of the case failed while it ended: {}", e.what());
    }
}

bool appbox::test::StartupRunHandle::WaitForSessions(std::uint32_t quiet_ms, std::uint32_t timeout_ms)
{
    return probe_run_ != nullptr && probe_run_->WaitForSessions(quiet_ms, timeout_ms);
}

const appbox::test::StartupRun& appbox::test::StartupRunHandle::Collect()
{
    if (probe_run_)
    {
        FillStartupRun(run_, probe_run_->CallAll<ProtocolStartupStarted>(StartupRequest(probe_data_)));
    }

    return run_;
}

std::size_t appbox::test::StartupRunHandle::Ping()
{
    if (!probe_run_)
    {
        return 0;
    }

    /*
     * The reports of the call are counted and not stored, so the answers a case
     * collected before stay the ones of its run.
     */
    return probe_run_->CallAll<ProtocolStartupStarted>(StartupRequest(probe_data_)).size();
}

const appbox::test::StartupRun& appbox::test::StartupRunHandle::Run() const
{
    return run_;
}

void appbox::test::StartupRunHandle::Exit(std::int32_t code)
{
    if (probe_run_)
    {
        probe_run_->ExitAll(code);
    }
}

std::uint32_t appbox::test::StartupRunHandle::Wait()
{
    if (!probe_run_)
    {
        return run_.exit_code;
    }

    run_.exit_code = probe_run_->Wait();
    run_.launcher_ms = probe_run_->LauncherLifetimeMs();
    return run_.exit_code;
}

std::uint32_t appbox::test::ProbeRunRawConfig(const std::wstring& cwd, const nlohmann::json& document,
                                              const LogRun& log, const std::wstring& config_file_name,
                                              std::uint32_t* launcher_pid, const std::vector<std::wstring>& args)
{
    /*
     * The document is written as it is, so a case can describe a configuration
     * the type of `LauncherConfig` cannot express: a member which is missing,
     * or a value of another type.
     */
    return static_cast<std::uint32_t>(
        RunLauncherJson(cwd, document, args, log, launcher_pid, std::wstring(), true, config_file_name));
}

appbox::test::StartupRun appbox::test::ProbeStartupRun(const std::wstring& cwd, const LauncherConfig& launcher_config,
                                                       const std::string& trigger, bool startup_screen,
                                                       const LogRun& log, bool use_configured_executable,
                                                       const std::wstring&   launcher_path,
                                                       const std::wstring&   working_directory,
                                                       const nlohmann::json& probe_data)
{
    StartupRunHandle handle(cwd, launcher_config, trigger, startup_screen, log, use_configured_executable,
                            launcher_path, working_directory, probe_data);

    if (handle.WaitForSessions())
    {
        handle.Collect();
    }

    handle.Exit(0);
    handle.Wait();
    return handle.Run();
}

std::uint32_t appbox::test::ProbeRunSecondary(const std::wstring& cwd, const nlohmann::json& document,
                                              const std::string& trigger, const std::string& marker,
                                              std::uint32_t exit_code, const LogRun& log, StartupRun& run,
                                              const std::wstring& working_directory, const nlohmann::json& probe_data,
                                              const std::wstring& launcher_path, const std::wstring& config_file_name)
{
    ProbeRun probe_run(cwd, [&](const std::string& key) {
        /*
         * The arguments of a second instance are the arguments of the request:
         * the master instance appends them to the arguments of every program it
         * starts for the request, so they are what makes those programs report
         * to the case.
         */
        const auto args = ProbeArgs(key, trigger, marker);

        /*
         * The directory of the client is the directory the request asks for, so
         * the launcher is started in the directory of the case rather than in
         * the directory the test process happens to work in.
         */
        const auto file_name = config_file_name.empty() ? std::wstring(kSecondaryConfigName) : config_file_name;
        return static_cast<std::uint32_t>(RunLauncherJson(cwd, document, args, log, &run.launcher_pid, launcher_path,
                                                          true, file_name, working_directory));
    });

    /*
     * The programs of the request connect after the second launcher left: it
     * leaves as soon as the master instance answered the request, while they
     * are still starting. A refused request starts no program of its own, so
     * the run ends the wait as soon as its launcher left with a non zero code.
     */
    const bool connected = probe_run.WaitForSessions(kProbeBindQuietMs, kSecondaryReportTimeoutMs, false);

    /*
     * The code of the launcher tells whether the request was served: zero is the
     * answer of a master instance which started the programs of the request, and
     * any other code is the answer of a master instance which refused it. A
     * refused request has nothing to report, because the master instance
     * terminates the programs it already started for it, so the reports are
     * collected for a served request only.
     */
    const bool served = probe_run.WaitForLauncher(kSecondaryReportTimeoutMs) && probe_run.ExitCode() == 0;

    /*
     * The lifetime of the client is the latency of the answer of the master
     * instance: the client leaves as soon as it received that answer, while the
     * programs of the request start and report afterwards.
     */
    run.launcher_ms = probe_run.LauncherLifetimeMs();

    if (connected && served)
    {
        FillStartupRun(run, probe_run.CallAll<ProtocolStartupStarted>(StartupRequest(probe_data)));
    }

    probe_run.ExitAll(static_cast<std::int32_t>(exit_code));
    run.exit_code = probe_run.Wait();
    return run.exit_code;
}

appbox::test::StartupRun appbox::test::ProbeStartupRunEmbedded(const std::wstring& cwd,
                                                               const std::wstring& launcher_path,
                                                               const std::string& trigger, const std::string& marker,
                                                               const LogRun& log)
{
    StartupRunHandle handle(cwd, launcher_path, trigger, marker, log);

    if (handle.WaitForSessions())
    {
        handle.Collect();
    }

    handle.Exit(0);
    handle.Wait();
    return handle.Run();
}

appbox::test::StartupRun appbox::test::ProbeStartupRunConfigFile(const std::wstring&   cwd,
                                                                 const std::wstring&   launcher_path,
                                                                 const LauncherConfig& launcher_config,
                                                                 const std::string& trigger, const LogRun& log)
{
    auto copy_config = OverrideConfig(launcher_config, ProbePath(ProbeBitness::Bit64));

    StartupRun run;
    ProbeRun   probe_run(cwd, [&](const std::string& key) {
        return static_cast<std::uint32_t>(
            RunLauncher(cwd, copy_config, ProbeArgs(key, trigger), log, &run.launcher_pid, launcher_path, true));
    });

    if (probe_run.WaitForSessions())
    {
        FillStartupRun(run, probe_run.CallAll<ProtocolStartupStarted>(ProtocolStartupStarted::Req{}));
    }

    probe_run.ExitAll(0);
    run.exit_code = probe_run.Wait();
    return run;
}

appbox::test::ProgramRun appbox::test::ProbeProgramRun(const std::wstring& cwd, const LauncherConfig& launcher_config,
                                                       const LogRun& log)
{
    /*
     * The programs of the configuration are started as they are, so the
     * configuration is only adapted to the machine the cases run on: the
     * console window of a program is hidden and the startup screen is turned
     * off.
     */
    appbox::LauncherConfig copy_config = launcher_config;
    copy_config.hide_console = true;
    copy_config.startup.screen = false;

    ProgramRun run;
    run.exit_code = static_cast<std::uint32_t>(RunLauncher(cwd, copy_config, {}, log));
    return run;
}

appbox::test::ShellRun appbox::test::ProbeShellRun(const std::wstring& cwd, const LauncherConfig& launcher_config,
                                                   const std::vector<std::string>& shell_command,
                                                   const std::string& trigger, const LogRun& log)
{
    auto copy_config = OverrideConfig(launcher_config, ProbePath(ProbeBitness::Bit64));

    std::vector<std::wstring> args;
    if (!trigger.empty())
    {
        args.push_back(L"--X-AppBox-Startup");
        args.push_back(CLI::widen(trigger));
    }

    /*
     * The shell takes the remaining arguments of the launcher as its command, so
     * the command follows the option itself and no probe process is added.
     */
    args.push_back(L"--X-AppBox-Shell");
    for (const auto& token : shell_command)
    {
        args.push_back(CLI::widen(token));
    }

    ShellRun run;
    ProbeRun probe_run(
        cwd, [&](const std::string&) { return static_cast<std::uint32_t>(RunLauncher(cwd, copy_config, args, log)); });

    /*
     * The command of the case is run by the shell instead of the probe, so the
     * run ends by itself. A probe which connected anyway is asked for its
     * report and ended at once: a probe which kept serving would keep the run
     * of the launcher alive, and the case would wait for the end of a run which
     * already did its work.
     */
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kShellTimeoutMs);
    bool       collected = false;

    while (!probe_run.Finished() && std::chrono::steady_clock::now() < deadline)
    {
        if (!collected && probe_run.SessionCount() > 0)
        {
            for (const auto& answer : probe_run.CallAll<ProtocolStartupStarted>(ProtocolStartupStarted::Req{}))
            {
                run.reported.push_back(answer.marker);
            }

            probe_run.ExitAll(0);
            collected = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }

    run.exit_code = probe_run.Wait();
    std::sort(run.reported.begin(), run.reported.end());
    return run;
}
