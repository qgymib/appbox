/*
 * The RPC headers pull in winsock2.h through asio, they have to come before
 * <windows.h> which would otherwise include the winsock.h version 1 header.
 */
#include "RemoteServer.hpp"
#include "RemoteClient.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <spdlog/spdlog.h>
#include <base64.hpp>
#include <CLI/Encoding.hpp>
#include "utils/Semaphore.hpp"
#include "launcher/Config.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include "BuildCommandLine.hpp"
#include "ProbeCall.hpp"
#include "MultiInstance.hpp"
#include "ProbePath.hpp"
#include "Test.hpp"
#include "TestSandboxId.hpp"

struct ProbeContext
{
    typedef std::shared_ptr<ProbeContext> Ptr;

    std::string                 name;         /* Probe name */
    nlohmann::json              data;         /* Probe data */
    nlohmann::json              result;       /* Probe result */
    std::vector<nlohmann::json> results;      /* Probe results of every process which reported */
    std::mutex                  result_mutex; /* Result mutex */
    appbox::Semaphore           sem;          /* Semaphore */
};
typedef std::map<std::string, ProbeContext::Ptr> ProbeContextMap;

struct ProbeServer
{
    typedef std::shared_ptr<ProbeServer> Ptr;

    std::string               pipe_path;  /* Pipe path */
    appbox::RemoteServer::Ptr rpc_server; /* RPC server */

    std::atomic_uint64_t rpc_id;            /* RPC ID generator */
    ProbeContextMap      context_map;       /* Context map */
    std::mutex           context_map_mutex; /* Context map mutex */
};

struct ProbeKey
{
    ProbeKey(const std::string& name, const nlohmann::json& data);
    ~ProbeKey();

    std::string       key;
    ProbeContext::Ptr ctx;
};

/**
 * @brief Global probe server context.
 */
static ProbeServer::Ptr s_probe_srv;

static void OnProbeRequest(uint64_t id, const nlohmann::json& req)
{
    appbox::test::ProbeRequest::Req c_req = req;

    ProbeContext::Ptr ctx;
    {
        std::lock_guard<std::mutex> lock(s_probe_srv->context_map_mutex);
        auto                        it = s_probe_srv->context_map.find(c_req.key);
        if (it == s_probe_srv->context_map.end())
        {
            appbox::RemoteError err;
            err.code = -1;
            err.message = "Probe key not found";
            s_probe_srv->rpc_server->SendResponse(id, tl::unexpected(err));
            return;
        }
        ctx = it->second;
    }

    appbox::test::ProbeRequest::Rsp c_rsp;
    c_rsp.name = ctx->name;
    c_rsp.data = ctx->data;

    nlohmann::json j_rsp = c_rsp;
    s_probe_srv->rpc_server->SendResponse(id, j_rsp);
}

static void OnProbeResponse(uint64_t id, const nlohmann::json& req)
{
    appbox::test::ProbeResponse::Req c_req = req;

    ProbeContext::Ptr ctx;
    {
        std::lock_guard<std::mutex> lock(s_probe_srv->context_map_mutex);
        auto                        it = s_probe_srv->context_map.find(c_req.key);
        if (it == s_probe_srv->context_map.end())
        {
            appbox::RemoteError err;
            err.code = -1;
            err.message = "Probe key not found";
            s_probe_srv->rpc_server->SendResponse(id, tl::unexpected(err));
            return;
        }
        ctx = it->second;
    }

    {
        /*
         * Several probe processes may report for the same key: the launcher
         * starts one process per startup file, so the results are collected in
         * a list as well.
         */
        std::lock_guard<std::mutex> lock(ctx->result_mutex);
        ctx->result = c_req.result;
        ctx->results.push_back(c_req.result);
    }

    /*
     * The semaphore is released before the acknowledgment is sent: the probe
     * waits for the acknowledgment before it leaves, so a received
     * acknowledgment proves that the result is visible to ProbeCall(). The
     * response is written by the IO thread after this handler returned, which
     * keeps the order of the two steps above intact.
     */
    ctx->sem.Release();

    appbox::test::ProbeResponse::Rsp c_rsp{};
    nlohmann::json                   j_rsp = c_rsp;
    s_probe_srv->rpc_server->SendResponse(id, j_rsp);
}

static void InitProbeServer()
{
    s_probe_srv = std::make_shared<ProbeServer>();

    {
        std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto        random_str = appbox::RandomString(16);
        s_probe_srv->pipe_path = fmt::format(R"(\\.\pipe\appbox-test-{}-{})", timestamp, random_str);
    }

    s_probe_srv->rpc_id = 0;
    s_probe_srv->rpc_server = appbox::RemoteServer::Create(s_probe_srv->pipe_path);

    s_probe_srv->rpc_server->RegisterMethod(appbox::test::ProbeRequest::Method, OnProbeRequest);
    s_probe_srv->rpc_server->RegisterMethod(appbox::test::ProbeResponse::Method, OnProbeResponse);

    s_probe_srv->rpc_server->Start();
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
static ProbeServer::Ptr EnsureProbeServer()
{
    static std::once_flag once;
    std::call_once(once, InitProbeServer);
    return s_probe_srv;
}

/**
 * @brief Generate a temporary key by process id and global counter.
 */
static std::string GenTempKey()
{
    auto pid = GetCurrentProcessId();
    return fmt::format("{}-{}", pid, s_probe_srv->rpc_id++);
}

ProbeKey::ProbeKey(const std::string& name, const nlohmann::json& data)
{
    key = GenTempKey();

    ctx = std::make_shared<ProbeContext>();
    ctx->name = name;
    ctx->data = data;

    {
        std::lock_guard lock(s_probe_srv->context_map_mutex);
        s_probe_srv->context_map.insert(ProbeContextMap::value_type(key, ctx));
    }
}

ProbeKey::~ProbeKey()
{
    std::lock_guard lock(s_probe_srv->context_map_mutex);
    s_probe_srv->context_map.erase(key);
}

/**
 * @brief Budget of a case which waits for the programs of a request to report.
 *
 * The programs of a request are created by the master instance before it
 * answers the request, so they need the time of a process start to reach the
 * probe server of the case.
 */
constexpr int kSecondaryReportTimeoutMs = 10000;

/**
 * @brief Wait until a probe of a key reported its result.
 *
 * @param[in] key Key of the report.
 * @param[in] timeout_ms Budget of the wait.
 */
void WaitForProbeResults(const ProbeKey& key, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(key.ctx->result_mutex);
            if (!key.ctx->results.empty())
            {
                return;
            }
        }

        if (std::chrono::steady_clock::now() >= deadline)
        {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
static appbox::LauncherConfig OverrideConfig(const appbox::LauncherConfig& config, const std::wstring& probe_path,
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
 * @return The exit code of the launcher.
 */
static DWORD RunLauncherJson(const std::wstring& cwd, const nlohmann::json& config,
                             const std::vector<std::wstring>& args, const appbox::test::LogRun& log,
                             std::uint32_t*      launcher_pid = nullptr,
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
 * @return The exit code of the launcher.
 */
static DWORD RunLauncher(const std::wstring& cwd, const appbox::LauncherConfig& config,
                         const std::vector<std::wstring>& args, const appbox::test::LogRun& log,
                         std::uint32_t* launcher_pid = nullptr, const std::wstring& executable_path = std::wstring(),
                         bool pass_config_file = true, const std::wstring& config_file_name = L"config.json",
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
 * @brief Run the launcher so it starts the probe process of a case.
 * @param[in] key Probe key passed to the probe processes.
 * @param[in] cwd The current working directory.
 * @param[in] config The launcher configuration.
 * @param[in] trigger Trigger for `--X-AppBox-Startup`, empty to omit the
 *                    option so the launcher starts the auto start files.
 * @param[in] log The log options of the run, see LogRun.
 * @param[out] launcher_pid Process id of the launcher which was started, may be
 *                          null.
 * @return The exit code of the launcher.
 */
static std::vector<std::wstring> ProbeArgs(const std::string& key, const std::string& trigger,
                                           const std::string& marker = std::string(), std::uint32_t hold_ms = 0,
                                           std::uint32_t exit_code = 0)
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

    /*
     * The time the probe stays alive after it reported is a global option of the
     * probe as well, so it precedes the subcommand for the same reason.
     */
    if (hold_ms != 0)
    {
        args.push_back(L"--probe_hold_ms");
        args.push_back(std::to_wstring(hold_ms));
    }

    /*
     * The exit code of the probe is a global option as well: a case which
     * checks the exit code of a run needs a program which leaves with a code of
     * its own.
     */
    if (exit_code != 0)
    {
        args.push_back(L"--probe_exit_code");
        args.push_back(std::to_wstring(exit_code));
    }

    args.push_back(L"probe");
    args.push_back(L"--probe_pipe");
    args.push_back(CLI::widen(s_probe_srv->pipe_path));
    args.push_back(L"--probe_key");
    args.push_back(CLI::widen(key));
    return args;
}

static DWORD RunLauncherForProbe(const std::string& key, const std::wstring& cwd, const appbox::LauncherConfig& config,
                                 const std::string& trigger, const appbox::test::LogRun& log,
                                 std::uint32_t*      launcher_pid = nullptr,
                                 const std::wstring& executable_path = std::wstring(), std::uint32_t hold_ms = 0,
                                 const std::wstring& working_directory = std::wstring())
{
    return RunLauncher(cwd, config, ProbeArgs(key, trigger, std::string(), hold_ms), log, launcher_pid, executable_path,
                       true, L"config.json", working_directory);
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
static void ReportSandboxLogs(const std::wstring& cwd)
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
 * @brief Run the launcher so it starts the probe process of a case.
 * @param[in] key Probe key passed to the probe process.
 * @param[in] cwd The current working directory.
 * @param[in] config The launcher configuration, already pointed at the probe.
 * @param[in] log The log options of the run, see LogRun.
 * @throws std::runtime_error The probe process did not leave with code 0.
 */
static void RunProbe(const std::string& key, const std::wstring& cwd, const appbox::LauncherConfig& config,
                     const appbox::test::LogRun& log)
{
    const auto exit_code = RunLauncherForProbe(key, cwd, config, std::string(), log);
    if (exit_code != 0)
    {
        ReportSandboxLogs(cwd);

        auto msg = fmt::format("Probe exited with code {}", exit_code);
        SPDLOG_ERROR(msg);
        throw std::runtime_error(msg);
    }
}

nlohmann::json appbox::test::ProbeCall(const std::string& name, const nlohmann::json& data, const std::wstring& cwd,
                                       const LauncherConfig& launcher_config, const LogRun& log, ProbeBitness bitness)
{
    EnsureProbeServer();

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

    ProbeKey key(name, data);
    RunProbe(key.key, cwd, copy_config, log);

    key.ctx->sem.Acquire();
    return key.ctx->result;
}

/**
 * @brief Collect the reports the probe processes of a run sent.
 * @param[in] key Probe key of the run.
 * @param[in,out] run Run to fill.
 */
static void CollectStartupResults(const ProbeKey& key, appbox::test::StartupRun& run)
{
    std::lock_guard<std::mutex> lock(key.ctx->result_mutex);
    for (const auto& result : key.ctx->results)
    {
        const auto marker = result.find("marker");
        if (marker != result.end() && marker->is_string())
        {
            run.started.push_back(marker->get<std::string>());
        }

        appbox::test::StartupReport report;
        if (marker != result.end() && marker->is_string())
        {
            report.marker = marker->get<std::string>();
        }
        report.pid = result.value("pid", 0u);
        report.parent_pid = result.value("parent_pid", 0u);
        report.module_path = result.value("module_path", std::string());
        report.command_line = result.value("command_line", std::string());
        if (result.contains("environment") && result["environment"].is_array())
        {
            report.environment = result["environment"].get<std::vector<std::string>>();
        }
        report.cwd = result.value("cwd", std::string());
        if (result.contains("relative_ops") && result["relative_ops"].is_array())
        {
            for (const auto& operation : result["relative_ops"])
            {
                appbox::test::StartupReport::RelativeOperation outcome;
                outcome.code = operation.value("code", 0u);
                outcome.data = operation.value("data", std::string());
                report.relative_ops.push_back(std::move(outcome));
            }
        }
        run.reports.push_back(std::move(report));
    }

    std::sort(run.started.begin(), run.started.end());
}

appbox::test::StartupRun appbox::test::ProbeStartupRun(const std::wstring& cwd, const LauncherConfig& launcher_config,
                                                       const std::string& trigger, bool startup_screen,
                                                       const LogRun& log, bool use_configured_executable,
                                                       const std::wstring& launcher_path, std::uint32_t hold_ms,
                                                       const std::wstring&   working_directory,
                                                       const nlohmann::json& probe_data)
{
    EnsureProbeServer();

    auto copy_config = OverrideConfig(launcher_config, ProbePath(ProbeBitness::Bit64), !use_configured_executable);
    copy_config.startup.screen = startup_screen;

    ProbeKey key("StartupStarted", probe_data);

    StartupRun run;
    run.exit_code = static_cast<std::uint32_t>(RunLauncherForProbe(
        key.key, cwd, copy_config, trigger, log, &run.launcher_pid, launcher_path, hold_ms, working_directory));
    CollectStartupResults(key, run);
    return run;
}

std::uint32_t appbox::test::ProbeRunSecondary(const std::wstring& cwd, const nlohmann::json& document,
                                              const std::string& trigger, const std::string& marker,
                                              std::uint32_t exit_code, const LogRun& log, StartupRun& run,
                                              const std::wstring& working_directory, const nlohmann::json& probe_data,
                                              const std::wstring& launcher_path, const std::wstring& config_file_name)
{
    EnsureProbeServer();

    ProbeKey key("StartupStarted", probe_data);

    /*
     * The arguments of a second instance are the arguments of the request: the
     * master instance appends them to the arguments of every program it starts
     * for the request, so they are what makes those programs report to the
     * case.
     */
    const auto args = ProbeArgs(key.key, trigger, marker, 0, exit_code);

    /*
     * The directory of the client is the directory the request asks for, so the
     * launcher is started in the directory of the case rather than in the
     * directory the test process happens to work in.
     */
    const auto file_name = config_file_name.empty() ? std::wstring(kSecondaryConfigName) : config_file_name;
    run.exit_code = static_cast<std::uint32_t>(RunLauncherJson(cwd, document, args, log, &run.launcher_pid,
                                                               launcher_path, true, file_name, working_directory));

    /*
     * The programs of the request report after the second launcher left: it
     * leaves as soon as the master instance created them, while they are still
     * starting. The key of the report is only known to this call, so the case
     * waits for the report before it releases the key. A refused request starts
     * no program of its own which could report, so the call does not wait.
     */
    if (run.exit_code == 0)
    {
        WaitForProbeResults(key, kSecondaryReportTimeoutMs);
    }

    CollectStartupResults(key, run);
    return run.exit_code;
}

appbox::test::StartupRun appbox::test::ProbeStartupRunEmbedded(const std::wstring& cwd,
                                                               const std::wstring& launcher_path,
                                                               const std::string& trigger, const std::string& marker,
                                                               const LogRun& log, std::uint32_t hold_ms)
{
    EnsureProbeServer();

    ProbeKey key("StartupStarted", nlohmann::json::object());

    StartupRun run;
    run.exit_code = static_cast<std::uint32_t>(RunLauncher(cwd, appbox::LauncherConfig{},
                                                           ProbeArgs(key.key, trigger, marker, hold_ms), log,
                                                           &run.launcher_pid, launcher_path, false));
    CollectStartupResults(key, run);
    return run;
}

appbox::test::StartupRun appbox::test::ProbeStartupRunConfigFile(const std::wstring&   cwd,
                                                                 const std::wstring&   launcher_path,
                                                                 const LauncherConfig& launcher_config,
                                                                 const std::string& trigger, const LogRun& log)
{
    EnsureProbeServer();

    auto copy_config = OverrideConfig(launcher_config, ProbePath(ProbeBitness::Bit64));

    ProbeKey key("StartupStarted", nlohmann::json::object());

    StartupRun run;
    run.exit_code = static_cast<std::uint32_t>(
        RunLauncher(cwd, copy_config, ProbeArgs(key.key, trigger), log, &run.launcher_pid, launcher_path, true));
    CollectStartupResults(key, run);
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
    EnsureProbeServer();

    auto copy_config = OverrideConfig(launcher_config, ProbePath(ProbeBitness::Bit64));

    ProbeKey key("StartupStarted", nlohmann::json::object());

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
    run.exit_code = static_cast<std::uint32_t>(RunLauncher(cwd, copy_config, args, log));

    /*
     * The command of the case is run by the shell instead of the probe, so no
     * report is waited for: a report which arrived before the launcher left was
     * sent by a startup file the launcher started although it had to ignore it.
     */
    {
        std::lock_guard<std::mutex> lock(key.ctx->result_mutex);
        for (const auto& result : key.ctx->results)
        {
            const auto marker = result.find("marker");
            if (marker != result.end() && marker->is_string())
            {
                run.reported.push_back(marker->get<std::string>());
            }
        }
    }

    std::sort(run.reported.begin(), run.reported.end());
    return run;
}
