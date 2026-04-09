#include <spdlog/spdlog.h>
#include "__init__.hpp"

#define PROBE_COMMAND "probe"

struct ProbeParam
{
    std::string connect_url;
};

appbox::test::ProbeCtx appbox::test::probe;
static ProbeParam      s_param;

static void ProbeMain()
{
    /* Create rpc client and start */
    appbox::test::probe.rpc_client = appbox::RemoteClient::Create(s_param.connect_url);
    if (!appbox::test::probe.rpc_client->Start())
    {
        SPDLOG_ERROR("Failed to start RPC client");
        throw std::runtime_error("Failed to start RPC client");
    }
}

void appbox::test::ProbeInit(CLI::App& app)
{
    auto cmd = app.add_subcommand(PROBE_COMMAND);
    cmd->add_option("--connect", s_param.connect_url)->required();

    cmd->final_callback([]() {
        ProbeMain();

        /* throw to exit success. */
        throw CLI::Success();
    });
}
