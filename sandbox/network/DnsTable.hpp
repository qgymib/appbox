#ifndef APPBOX_SANDBOX_NETWORK_DNSTABLE_HPP
#define APPBOX_SANDBOX_NETWORK_DNSTABLE_HPP

#include "NetworkIsolation.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{
namespace network
{

/**
 * @brief Address family a name resolution asked for.
 *
 * A resolution asks for one family, for both or for whatever the caller
 * accepts; a redirection is answered only when the family of its address fits
 * the question.
 */
enum class RequestedFamily
{
    Any,  ///< The caller accepts an IPv4 and an IPv6 address.
    IPv4, ///< The caller asked for an IPv4 address.
    IPv6  ///< The caller asked for an IPv6 address.
};

/**
 * @brief One DNS redirection of the network isolation file.
 */
struct DnsEntry
{
    /**
     * @brief Hostname the entry redirects, normalized for the comparison.
     */
    std::string hostname;

    /**
     * @brief Address the hostname resolves to inside the sandbox.
     *
     * The address is the IPv4 or IPv6 literal the packer stored, which is the
     * text the name resolution is answered with.
     */
    std::string redirect;

    /**
     * @brief Family of the redirect address.
     */
    network_isolation::AddressFamily family = network_isolation::AddressFamily::IPv4;
};

/**
 * @brief The DNS redirections of the network isolation file of the sandbox.
 *
 * The table holds the entries the packer wrote into the network isolation file
 * (see `common/NetworkIsolation.hpp` for the schema). The name resolution hooks
 * ask it for the address a hostname has to resolve to: a name the table knows
 * is answered from the entry instead of being asked at the host, a name it does
 * not know keeps the resolution of the host.
 *
 * The class holds no wxWidgets and no Windows dependency, so its rules are unit
 * testable.
 */
class DnsTable
{
public:
    /**
     * @brief Read the DNS redirections of an isolation file into the table.
     *
     * The call merges the document into the table instead of replacing it: the
     * table holds the redirections of the layers below the file, a hostname
     * the document lists overrides the entry of the same hostname of those
     * layers, and a hostname it does not list keeps the entry below it. The
     * merge is applied after the whole document was accepted, so a malformed
     * file leaves the table untouched.
     *
     * A document is rejected when it is not a JSON object, when its version is
     * not the version of the schema, when the entry list is not an array, or
     * when an entry is not an object with a non empty hostname and a redirect
     * which is an IPv4 or an IPv6 address literal. A hostname which is listed
     * twice keeps the last entry.
     *
     * @param[in] text Text of the isolation file.
     * @param[out] error Error description on failure.
     * @return true when the document was accepted.
     */
    bool Parse(const std::string& text, std::string& error);

    /**
     * @brief Find the redirection of a hostname.
     *
     * The hostname is compared ignoring the case and a trailing dot, because
     * the name resolution of the host does the same.
     *
     * @param[in] hostname Hostname the application asked for.
     * @param[in] family Family the application asked for.
     * @return The entry of the hostname, null when the name is not redirected
     *         or when the family of its address does not fit the question.
     */
    const DnsEntry* Find(const std::string& hostname, RequestedFamily family) const;

    /**
     * @brief Number of redirections the table holds.
     * @return The number of entries.
     */
    std::size_t Count() const;

    /**
     * @brief Drop every redirection.
     */
    void Clear();

private:
    /**
     * @brief The redirections in the order of the isolation file.
     */
    std::vector<DnsEntry> entries_;
};

} // namespace network
} // namespace appbox

#endif // APPBOX_SANDBOX_NETWORK_DNSTABLE_HPP
