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
#include <memory>
#include <map>
#include <mutex>
#include <vector>
#include <spdlog/spdlog.h>
#include <base64.hpp>
#include <CLI/Encoding.hpp>
#include "utils/Semaphore.hpp"
#include "loader/Config.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include "BuildCommandLine.hpp"
#include "ProbeCall.hpp"
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

    std::string               exe_path;   /* Self executable path */
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
         * Several probe processes may report for the same key: the loader
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

static std::wstring GetExePath()
{
    std::wstring buf(MAX_PATH, L'\0');
    while (true)
    {
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len < buf.size())
        {
            buf.resize(len);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

static void InitProbeServer()
{
    s_probe_srv = std::make_shared<ProbeServer>();

    {
        auto exe_path = GetExePath();
        s_probe_srv->exe_path = appbox::WideToUTF8(exe_path.c_str());
    }

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
 * the test executable instead of the program of the case.
 *
 * The console window of the probe is hidden as well: the probe is a console
 * program, while the loader and its launcher are GUI programs without a
 * console, so Windows would open a console window for every probe process of
 * every case and that window would pop up on the desktop of the machine the
 * cases run on.
 *
 * @param[in] config Configuration of the case.
 * @return The configuration of the probe run.
 */
static appbox::LoaderConfig OverrideConfig(const appbox::LoaderConfig& config)
{
    appbox::LoaderConfig copy_config = config;

    if (copy_config.startups.empty())
    {
        /*
         * A configuration of a case which does not describe startup files
         * starts the probe once, like the single main program of the packaged
         * application.
         */
        appbox::LoaderStartup startup;
        startup.trigger = "probe";
        startup.auto_start = true;
        copy_config.startups.push_back(std::move(startup));
    }

    /*
     * The probe runs itself instead of the packaged application, so every
     * startup file points at the probe executable.
     */
    for (auto& startup : copy_config.startups)
    {
        startup.executable = s_probe_srv->exe_path;
    }

    copy_config.hide_console = true;

    return copy_config;
}

/**
 * @brief Run the loader of a configuration and wait for it.
 *
 * The configuration is written beside the working directory of the case, which
 * is also the directory the loader resolves its sandbox layout against.
 *
 * @param[in] cwd The current working directory.
 * @param[in] config The loader configuration.
 * @param[in] args Arguments of the loader, without the options which select
 *                 and log the configuration.
 * @return The exit code of the loader.
 */
static DWORD RunLoader(const std::wstring& cwd, const appbox::LoaderConfig& config,
                       const std::vector<std::wstring>& args)
{
    std::filesystem::path cfg_path;
    {
        nlohmann::json j_config = config;
        auto           data = j_config.dump(2);

        cfg_path = std::filesystem::path(cwd) / "config.json";
        std::ofstream ofs(cfg_path, std::ios::binary | std::ios::trunc);
        ofs.write(data.data(), data.size());
    }

    auto log_path = std::filesystem::path(cwd) / "log.txt";

    std::vector<std::wstring> full_args = {
        L"--X-AppBox-ConfigFile",       cfg_path.wstring(),    L"--X-AppBox-LogLevel",
        appbox::test::config.log_level, L"--X-AppBox-LogFile", log_path.wstring(),
    };
    full_args.insert(full_args.end(), args.begin(), args.end());

    auto cmd = appbox::BuildCommandLine(appbox::test::config.loader_path, full_args);

    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));

    if (!CreateProcessW(appbox::test::config.loader_path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &startup_info, &process_info))
    {
        auto msg = fmt::format("CreateProcessW failed: {}", GetLastError());
        SPDLOG_ERROR(msg);
        throw std::runtime_error(msg);
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
 * @brief Run the loader so it starts the probe process of a case.
 * @param[in] key Probe key passed to the probe processes.
 * @param[in] cwd The current working directory.
 * @param[in] config The loader configuration.
 * @param[in] trigger Trigger for `--X-AppBox-Startup`, empty to omit the
 *                    option so the loader starts the auto start files.
 * @return The exit code of the loader.
 */
static DWORD RunLoaderForProbe(const std::string& key, const std::wstring& cwd, const appbox::LoaderConfig& config,
                               const std::string& trigger)
{
    std::vector<std::wstring> args;
    if (!trigger.empty())
    {
        args.push_back(L"--X-AppBox-Startup");
        args.push_back(CLI::widen(trigger));
    }

    args.push_back(L"probe");
    args.push_back(L"--probe_pipe");
    args.push_back(CLI::widen(s_probe_srv->pipe_path));
    args.push_back(L"--probe_key");
    args.push_back(CLI::widen(key));

    return RunLoader(cwd, config, args);
}

static void RunSelfAsProbe(const std::string& key, const std::wstring& cwd, const appbox::LoaderConfig& config)
{
    const auto exit_code = RunLoaderForProbe(key, cwd, config, std::string());
    if (exit_code != 0)
    {
        auto msg = fmt::format("Probe exited with code {}", exit_code);
        SPDLOG_ERROR(msg);
        throw std::runtime_error(msg);
    }
}

nlohmann::json appbox::test::ProbeCall(const std::string& name, const nlohmann::json& data, const std::wstring& cwd,
                                       const LoaderConfig& loader_config)
{
    static std::once_flag once;
    std::call_once(once, InitProbeServer);

    auto copy_config = OverrideConfig(loader_config);

    ProbeKey key(name, data);
    RunSelfAsProbe(key.key, cwd, copy_config);

    key.ctx->sem.Acquire();
    return key.ctx->result;
}

appbox::test::StartupRun appbox::test::ProbeStartupRun(const std::wstring& cwd, const LoaderConfig& loader_config,
                                                       const std::string& trigger)
{
    static std::once_flag once;
    std::call_once(once, InitProbeServer);

    auto copy_config = OverrideConfig(loader_config);

    ProbeKey key("StartupStarted", nlohmann::json::object());

    StartupRun run;
    run.exit_code = static_cast<std::uint32_t>(RunLoaderForProbe(key.key, cwd, copy_config, trigger));

    {
        std::lock_guard<std::mutex> lock(key.ctx->result_mutex);
        for (const auto& result : key.ctx->results)
        {
            const auto marker = result.find("marker");
            if (marker != result.end() && marker->is_string())
            {
                run.started.push_back(marker->get<std::string>());
            }
        }
    }

    std::sort(run.started.begin(), run.started.end());
    return run;
}

appbox::test::ShellRun appbox::test::ProbeShellRun(const std::wstring& cwd, const LoaderConfig& loader_config,
                                                   const std::vector<std::string>& shell_command,
                                                   const std::string&              trigger)
{
    static std::once_flag once;
    std::call_once(once, InitProbeServer);

    auto copy_config = OverrideConfig(loader_config);

    ProbeKey key("StartupStarted", nlohmann::json::object());

    std::vector<std::wstring> args;
    if (!trigger.empty())
    {
        args.push_back(L"--X-AppBox-Startup");
        args.push_back(CLI::widen(trigger));
    }

    /*
     * The shell takes the remaining arguments of the loader as its command, so
     * the command follows the option itself and no probe process is added.
     */
    args.push_back(L"--X-AppBox-Shell");
    for (const auto& token : shell_command)
    {
        args.push_back(CLI::widen(token));
    }

    ShellRun run;
    run.exit_code = static_cast<std::uint32_t>(RunLoader(cwd, copy_config, args));

    /*
     * The command of the case is run by the shell instead of the probe, so no
     * report is waited for: a report which arrived before the loader left was
     * sent by a startup file the loader started although it had to ignore it.
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
