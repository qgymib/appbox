/*
 * The RPC headers pull in winsock2.h through asio, they have to come before
 * <windows.h> which would otherwise include the winsock.h version 1 header.
 */
#include "RemoteServer.hpp"
#include "RemoteClient.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <map>
#include <mutex>
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
#include "ProbePath.hpp"
#include "Test.hpp"

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
 * @brief Run the launcher of a configuration and wait for it.
 *
 * The configuration is written beside the working directory of the case, which
 * is also the directory the launcher resolves its sandbox layout against.
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
 * @return The exit code of the launcher.
 */
static DWORD RunLauncher(const std::wstring& cwd, const appbox::LauncherConfig& config,
                         const std::vector<std::wstring>& args, const appbox::test::LogRun& log,
                         std::uint32_t* launcher_pid = nullptr, const std::wstring& executable_path = std::wstring(),
                         bool pass_config_file = true)
{
    const auto& launcher_path = executable_path.empty() ? appbox::test::config.launcher_path : executable_path;

    std::vector<std::wstring> full_args;
    if (pass_config_file)
    {
        std::filesystem::path cfg_path;
        {
            nlohmann::json j_config = config;
            auto           data = j_config.dump(2);

            cfg_path = std::filesystem::path(cwd) / "config.json";
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

    if (!CreateProcessW(launcher_path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup_info,
                        &process_info))
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
     */
    if (!marker.empty())
    {
        args.push_back(L"--startup_marker");
        args.push_back(CLI::widen(marker));
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
                                 std::uint32_t* launcher_pid = nullptr)
{
    return RunLauncher(cwd, config, ProbeArgs(key, trigger), log, launcher_pid);
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
    static std::once_flag once;
    std::call_once(once, InitProbeServer);

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
        run.reports.push_back(std::move(report));
    }

    std::sort(run.started.begin(), run.started.end());
}

appbox::test::StartupRun appbox::test::ProbeStartupRun(const std::wstring& cwd, const LauncherConfig& launcher_config,
                                                       const std::string& trigger, bool startup_screen,
                                                       const LogRun& log, bool use_configured_executable)
{
    static std::once_flag once;
    std::call_once(once, InitProbeServer);

    auto copy_config = OverrideConfig(launcher_config, ProbePath(ProbeBitness::Bit64), !use_configured_executable);
    copy_config.startup.screen = startup_screen;

    ProbeKey key("StartupStarted", nlohmann::json::object());

    StartupRun run;
    run.exit_code =
        static_cast<std::uint32_t>(RunLauncherForProbe(key.key, cwd, copy_config, trigger, log, &run.launcher_pid));
    CollectStartupResults(key, run);
    return run;
}

appbox::test::StartupRun appbox::test::ProbeStartupRunEmbedded(const std::wstring& cwd,
                                                               const std::wstring& launcher_path,
                                                               const std::string& trigger, const std::string& marker,
                                                               const LogRun& log)
{
    static std::once_flag once;
    std::call_once(once, InitProbeServer);

    ProbeKey key("StartupStarted", nlohmann::json::object());

    StartupRun run;
    run.exit_code =
        static_cast<std::uint32_t>(RunLauncher(cwd, appbox::LauncherConfig{}, ProbeArgs(key.key, trigger, marker), log,
                                               &run.launcher_pid, launcher_path, false));
    CollectStartupResults(key, run);
    return run;
}

appbox::test::StartupRun appbox::test::ProbeStartupRunConfigFile(const std::wstring&   cwd,
                                                                 const std::wstring&   launcher_path,
                                                                 const LauncherConfig& launcher_config,
                                                                 const std::string& trigger, const LogRun& log)
{
    static std::once_flag once;
    std::call_once(once, InitProbeServer);

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
    static std::once_flag once;
    std::call_once(once, InitProbeServer);

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
