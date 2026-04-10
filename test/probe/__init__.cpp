#include <spdlog/spdlog.h>
#include "HelloWorld.hpp"
#include "__init__.hpp"

#define PROBE_COMMAND "probe"

typedef void (*ProbeInitFn)(CLI::App&);

static ProbeInitFn     s_probes[] = { &appbox::test::RegisterProbeHelloWorld };
appbox::test::ProbeCtx appbox::test::probe;

void appbox::test::ProbeInit(CLI::App& app)
{
    auto fn = [](const std::string& url) {
        appbox::test::probe.rpc_client = appbox::RemoteClient::Create(url);
        if (!appbox::test::probe.rpc_client->Start())
        {
            SPDLOG_ERROR("Failed to start RPC client");
            throw std::runtime_error("Failed to start RPC client");
        }
    };
    app.add_subcommand(PROBE_COMMAND)
        ->add_option_function<std::string>("--connect", fn)
        ->required();

    for (auto f : s_probes)
    {
        f(app);
    }
}
