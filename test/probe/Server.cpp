#include <windows.h>
#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include <spdlog/spdlog.h>
#include <base64.hpp>
#include <CLI/Encoding.hpp>
#include "utils/Semaphore.hpp"
#include "loader/Config.hpp"
#include "RemoteServer.hpp"
#include "Random.hpp"
#include "Server.hpp"
#include "WString.hpp"
#include "BuildCommandLine.hpp"
#include "Test.hpp"

struct ProbeContext
{
    typedef std::shared_ptr<ProbeContext> Ptr;

    std::string       name;   /* Probe name */
    nlohmann::json    data;   /* Probe data */
    nlohmann::json    result; /* Probe result */
    appbox::Semaphore sem;    /* Semaphore */
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

    ctx->result = c_req.result;
    ctx->sem.Release();
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

static void RunSelfAsProbe(const std::string& key)
{
    std::wstring cmd;
    {
        appbox::LoaderConfig config;
        config.launch.executable = s_probe_srv->exe_path;

        nlohmann::json j_config = config;
        auto           b_config = base64::to_base64(j_config.dump());

        /* clang-format off */
        cmd = appbox::BuildCommandLine(appbox::test::cmd_param.loader_path,
            {
                L"--X-AppBox-LogLevel", appbox::test::cmd_param.log_level,
                L"--X-AppBox-ConfigBase64", CLI::widen(b_config),
                L"probe",
                L"--probe_pipe", CLI::widen(s_probe_srv->pipe_path),
                L"--probe_key", CLI::widen(key),
            }
        );
        /* clang-format on */
    }

    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));

    auto create_ret = CreateProcessW(appbox::test::cmd_param.loader_path.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                                     0, nullptr, nullptr, &startup_info, &process_info);
    if (!create_ret)
    {
        auto msg = fmt::format("CreateProcessW failed: {}", GetLastError());
        SPDLOG_ERROR(msg);
        throw std::runtime_error(msg);
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);
    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
}

nlohmann::json appbox::test::ProbeCall(const std::string& name, const nlohmann::json& data)
{
    SPDLOG_INFO("Init probe server");
    static std::once_flag once;
    std::call_once(once, InitProbeServer);

    ProbeKey key(name, data);
    RunSelfAsProbe(key.key);

    key.ctx->sem.Acquire();
    return key.ctx->result;
}
