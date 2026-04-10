#include "HelloWorld.hpp"
#include "__init__.hpp"

struct ProbeHelloWorldParam
{
    std::string method = "Hello";
};

static ProbeHelloWorldParam s_param;

void appbox::test::RegisterProbeHelloWorld(CLI::App& app)
{
    auto cmd = app.add_subcommand("HelloWorld");
    cmd->add_option("--method", s_param.method);

    cmd->final_callback([]() {
        /* Send request and wait for response. */
        auto rsp = appbox::test::probe.rpc_client->Call(s_param.method, "Hello").get();
        if (rsp.has_value())
        {
            if (rsp == "World")
            {
                throw CLI::RuntimeError(EXIT_SUCCESS);
            }
        }
        throw CLI::RuntimeError(EXIT_FAILURE);
    });
}
