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

/**
 * @brief Marker of the startup file which started this process.
 */
static std::string s_startup_marker;

/**
 * @brief Time this process stays alive after it reported its result.
 *
 * A case which runs another launcher while this process works needs a program
 * which outlives the handshake: a probe which leaves as soon as it reported
 * would end the run of the launcher which started it, and the case would race
 * with the end of the run instead of observing it.
 */
static std::uint32_t s_probe_hold_ms = 0;

/**
 * @brief Exit code this process reports after it sent its result.
 *
 * A case which checks that the exit code of a program a request started does
 * not decide the exit code of the run needs a program which leaves with a code
 * of its own; the code of the probe is zero unless the case names one.
 */
static std::uint32_t s_probe_exit_code = 0;

/**
 * @brief Store the marker a case names for this process.
 *
 * The option exists beside `--startup_marker` because the launcher appends the
 * arguments of a request to the arguments of the startup file, which already
 * carries the marker of the file: an option which is given twice is refused by
 * the command line parser, so a request names the marker of its own processes
 * with an option of its own, which overrides the marker of the file.
 *
 * @param[in] marker Marker of the process.
 */
static void SetProbeMarker(const std::string& marker)
{
    s_startup_marker = marker;
}

static void ParseProbePipe(const std::wstring& url)
{
    SPDLOG_INFO("connect the probe to '{}'", CLI::narrow(url));

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

    SPDLOG_INFO("run the probe task '{}'", name);

    appbox::test::ProbeResponse::Req ret;
    ret.key = s_probe_ctx->probe_key;
    ret.result = ProbeCallLocal(name, data);

    /*
     * The report has to be acknowledged before this process leaves: the message
     * is written asynchronously by the IO thread of the client, so leaving
     * right after the call could drop it and the caller of ProbeCall() would
     * wait for the result of this probe forever.
     */
    auto ack = s_probe_ctx->rpc_client->Call<appbox::test::ProbeResponse>(ret).get();
    if (!ack.has_value())
    {
        SPDLOG_ERROR("ProbeResponse failed: {}", ack.error().message);
        throw CLI::RuntimeError(EXIT_FAILURE);
    }

    SPDLOG_INFO("the probe task '{}' reported its result", name);

    /*
     * The result is visible to the case now, so a probe which is asked to stay
     * alive keeps the run of its launcher alive as well.
     */
    if (s_probe_hold_ms != 0)
    {
        ::Sleep(s_probe_hold_ms);
    }

    /*
     * A program which leaves with a code of its own is what a case which checks
     * the exit code of a run needs: the code is reported after the result of
     * this process is visible to the case.
     */
    if (s_probe_exit_code != 0)
    {
        throw CLI::RuntimeError(static_cast<int>(s_probe_exit_code));
    }

    throw CLI::Success();
}

void appbox::test::ProbeInit(CLI::App& app)
{
    s_probe_ctx = std::make_shared<ProbeCtx>();

    app.add_option("--startup_marker", s_startup_marker, "Marker of the startup file which started this process");
    app.add_option_function<std::string>("--probe_marker", SetProbeMarker,
                                         "Marker of this process, which overrides the marker of the startup file");
    app.add_option("--probe_hold_ms", s_probe_hold_ms,
                   "Time this process stays alive after it reported its result, in milliseconds");
    app.add_option("--probe_exit_code", s_probe_exit_code, "Exit code this process reports after it sent its result");

    auto cmd = app.add_subcommand(PROBE_COMMAND);
    cmd->add_option_function<std::wstring>("--probe_pipe", ParseProbePipe)->required();
    cmd->add_option("--probe_key", s_probe_ctx->probe_key)->required();
    cmd->final_callback(ProbeRun);
}

const std::string& appbox::test::StartupMarker()
{
    return s_startup_marker;
}
