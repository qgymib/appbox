#ifndef APPBOX_TEST_UTILS_NETWORK_ISOLATION_BUILDER_HPP
#define APPBOX_TEST_UTILS_NETWORK_ISOLATION_BUILDER_HPP

#include "NetworkIsolation.hpp"
#include <filesystem>
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
 * @brief Proxy of the network isolation file of a test sandbox.
 *
 * The structure describes the optional `proxy` member of the file, which is
 * written only while the configuration carries a protocol or a field, like the
 * member the packer writes.
 */
struct NetworkIsolationProxy
{
    /**
     * @brief Whether the TCP traffic of the application is proxied.
     */
    bool tcp = false;

    /**
     * @brief Whether the UDP traffic of the application is proxied.
     */
    bool udp = false;

    /**
     * @brief Hostname or address of the proxy server.
     */
    std::wstring server;

    /**
     * @brief Port of the proxy server.
     */
    std::wstring port;

    /**
     * @brief Optional user name.
     */
    std::wstring username;

    /**
     * @brief Optional password.
     */
    std::wstring password;

    /**
     * @brief Whether the configuration carries a value.
     * @return true when a protocol is proxied or a field carries a value.
     */
    bool IsConfigured() const
    {
        return tcp || udp || !server.empty() || !port.empty() || !username.empty() || !password.empty();
    }
};

/**
 * @brief Write the network isolation file of a test sandbox.
 *
 * The file describes the DNS redirections of the packaged application and lives
 * in the network domain of the resources of the case, which is where the loader
 * looks for it (`<case root>/app/network/isolation.json`). The document is
 * built from the schema structure of `common/NetworkIsolation.hpp` instead of
 * through the packer, so a case also pins that the sandbox accepts a file which
 * the workspace did not write.
 *
 * @param[in] case_root Root directory of the case, normally the working
 *                      directory.
 * @param[in] entries Entries to list.
 * @return true on success.
 */
bool WriteNetworkIsolationFile(const std::filesystem::path&              case_root,
                               const std::vector<NetworkIsolationEntry>& entries);

/**
 * @brief Write the network isolation file of a test sandbox with a proxy.
 *
 * The `proxy` member is written only while the configuration carries a value,
 * which is the way the packer writes it as well: a case which passes an empty
 * configuration pins the document of a session without a proxy.
 *
 * @param[in] case_root Root directory of the case, normally the working
 *                      directory.
 * @param[in] entries Entries to list.
 * @param[in] proxy Proxy of the packaged application.
 * @return true on success.
 */
bool WriteNetworkIsolationFile(const std::filesystem::path&              case_root,
                               const std::vector<NetworkIsolationEntry>& entries, const NetworkIsolationProxy& proxy);

/**
 * @brief Write the text of the network isolation file of a test sandbox.
 *
 * The call writes the text as it is, so a case can pin what the sandbox does
 * with a document which is malformed or which carries members the builder does
 * not know.
 *
 * @param[in] case_root Root directory of the case, normally the working
 *                      directory.
 * @param[in] text Text of the isolation file.
 * @return true on success.
 */
bool WriteNetworkIsolationFileText(const std::filesystem::path& case_root, const std::string& text);

/**
 * @brief Build the text of a network isolation file.
 *
 * The call is the document builder of the write helpers: it returns the text
 * the file carries without writing a file, so a case which writes the document
 * somewhere else (a patch package, for example) shares the very same document.
 *
 * @param[in] entries Entries to list.
 * @param[in] proxy Proxy of the packaged application, written only while the
 *                  configuration carries a value.
 * @return The text of the document.
 */
std::string BuildNetworkIsolationText(const std::vector<NetworkIsolationEntry>& entries,
                                      const NetworkIsolationProxy&              proxy = {});

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_NETWORK_ISOLATION_BUILDER_HPP
