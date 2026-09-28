#ifndef APPBOX_PACKER_CORE_NETWORK_ISOLATION_FILE_HPP
#define APPBOX_PACKER_CORE_NETWORK_ISOLATION_FILE_HPP

#include "NetworkModel.hpp"
#include <string>

namespace appbox
{

/**
 * @brief Build the network isolation file of the network workspace.
 *
 * The document carries the network configuration of the workspace from the
 * packer to the sandbox (see `common/NetworkIsolation.hpp` for the schema):
 * every DNS redirection is listed with the hostname it redirects and the
 * address the name resolves to inside the sandbox, and the proxy is written as
 * the optional `proxy` member while the workspace holds one. The packer writes
 * the file into the overlay of the archive, the loader hands its path to the
 * sandbox, and the sandbox answers a name resolution of the packaged
 * application from it and sends its traffic through the proxy.
 *
 * The entries are written in the order of the model, which keeps the document
 * stable for a given model. The `proxy` member is left out while the model
 * holds no proxy configuration, so the document of a session without a proxy
 * is the one a file of the previous schema holds.
 *
 * @param[in] model The network model to describe.
 * @param[out] text The UTF-8 text of the isolation file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool BuildNetworkIsolationFile(const NetworkModel& model, std::string& text, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_NETWORK_ISOLATION_FILE_HPP
