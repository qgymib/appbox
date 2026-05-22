#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <mutex>
#include "RemoteClient.hpp"
#include "__init__.hpp"

#define PROBE_COMMAND "probe"

struct ProbeCtx
{
    typedef std::shared_ptr<ProbeCtx> Ptr;

    appbox::RemoteClient::Ptr rpc_client; /* RPC client */
    std::string               probe_key;
};

/**
 * @brief Probe command context.
 */
static ProbeCtx::Ptr s_probe_ctx;

static void ParseProbePipe(const std::wstring& url)
{
    s_probe_ctx->rpc_client = appbox::RemoteClient::Create(CLI::narrow(url));
    if (!s_probe_ctx->rpc_client->Start())
    {
        SPDLOG_ERROR("Failed to start RPC client");
        throw std::runtime_error("Failed to start RPC client");
    }
}

static nlohmann::json GetProbeRequest(std::string& name)
{
    appbox::test::ProbeRequest::Req req;
    req.key = s_probe_ctx->probe_key;

    auto rsp = s_probe_ctx->rpc_client->Call<appbox::test::ProbeRequest>(req).get();
    if (!rsp.has_value())
    {
        SPDLOG_ERROR("ProbeRequest failed");
        throw CLI::RuntimeError(EXIT_FAILURE);
    }

    name = rsp->name;
    return rsp->data;
}

static nlohmann::json ProbeCallLocal(const std::string& name, const nlohmann::json& data)
{
    auto& m = appbox::test::Probe::GetMap();

    auto it = m.find(name);
    if (it == m.end())
    {
        throw std::invalid_argument(name + " does not exist");
    }

    auto probe = it->second;
    return probe(data);
}

static void ProbeRun()
{
    std::string name;
    auto        data = GetProbeRequest(name);

    appbox::test::ProbeResponse::Req ret;
    ret.key = s_probe_ctx->probe_key;
    ret.result = ProbeCallLocal(name, data);

    s_probe_ctx->rpc_client->Call<appbox::test::ProbeResponse>(ret);
    throw CLI::Success();
}

void appbox::test::ProbeInit(CLI::App& app)
{
    s_probe_ctx = std::make_shared<ProbeCtx>();

    auto cmd = app.add_subcommand(PROBE_COMMAND);
    cmd->add_option_function<std::wstring>("--probe_pipe", ParseProbePipe)->required();
    cmd->add_option("--probe_key", s_probe_ctx->probe_key)->required();
    cmd->final_callback(ProbeRun);
}
