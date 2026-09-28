#ifndef APPBOX_TEST_UTILS_NETWORK_ISOLATION_BUILDER_HPP
#define APPBOX_TEST_UTILS_NETWORK_ISOLATION_BUILDER_HPP

#include "NetworkIsolation.hpp"
#include "loader/Config.hpp"
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief One DNS redirection of the network isolation file of a test sandbox.
 */
struct NetworkIsolationEntry
{
    /**
     * @brief Hostname or IP address which is redirected.
     */
    std::wstring hostname;

    /**
     * @brief Address the hostname resolves to inside the sandbox.
     */
    std::wstring redirect;
};

/**
 * @brief Write the network isolation file of a test sandbox.
 *
 * The file describes the DNS redirections of the packaged application and lives
 * in the overlay root of the configuration, which is where the loader looks for
 * it (`<overlay>/network-isolation.json`). The document is built directly
 * instead of through the packer, so a case also pins the schema a hand written
 * file uses.
 *
 * @param[in] config Loader configuration of the case.
 * @param[in] entries Entries to list.
 * @return true on success.
 */
bool WriteNetworkIsolationFile(const appbox::LoaderConfig& config, const std::vector<NetworkIsolationEntry>& entries);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_NETWORK_ISOLATION_BUILDER_HPP
