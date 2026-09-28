#ifndef APPBOX_SANDBOX_NETWORK_ISOLATION_HPP
#define APPBOX_SANDBOX_NETWORK_ISOLATION_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include "DnsTable.hpp"

namespace appbox
{
namespace network
{

/**
 * @brief The DNS redirections of the network isolation of the sandbox.
 *
 * The module loads the isolation file the packer wrote into the overlay of the
 * archive (see `common/NetworkIsolation.hpp` for the schema) and fills the
 * table of the sandbox instance with the redirections of the workspace. The
 * loader passes the path of the file in the injected configuration.
 *
 * A missing file, a missing configuration or a malformed document is not an
 * error: the sandbox then behaves like one without an isolation file, in which
 * every name is resolved by the host. Only a failure of the whole
 * initialization is reported, which is what the module table expects.
 */
class Isolation
{
public:
    /**
     * @brief Load the DNS redirections of the network workspace.
     *
     * The module has to be initialized before the hooks are attached, so the
     * file is read through the original entry points of the process.
     *
     * @return Status code.
     */
    static NTSTATUS Init();

    /**
     * @brief Drop the DNS redirections.
     */
    static void Exit();
};

} // namespace network
} // namespace appbox

#endif // APPBOX_SANDBOX_NETWORK_ISOLATION_HPP
