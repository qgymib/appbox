#ifndef APPBOX_TEST_PROBE_INIT_HPP
#define APPBOX_TEST_PROBE_INIT_HPP

#include <CLI/CLI.hpp>
#include "RemoteClient.hpp"

namespace appbox::test
{

struct ProbeCtx
{
    RemoteClient::Ptr rpc_client; /* RPC client */
};

/**
 * @brief Probe command context.
 */
extern ProbeCtx probe;

/**
 * @brief Initialize the probe command and its parameters
 * @param[in,out] app Command line application instance.
 */
void ProbeInit(CLI::App& app);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_INIT_HPP
