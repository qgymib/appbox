#ifndef APPBOX_PACKER_CORE_NETWORK_MODEL_HPP
#define APPBOX_PACKER_CORE_NETWORK_MODEL_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace appbox
{

/**
 * @brief One DNS redirection of the Network workspace.
 *
 * The entry names a hostname or an IP address the sandboxed application asks
 * for and the address that name is redirected to, which is the pair of the
 * `Hostname or IP Address` and `Redirect` columns of the workspace.
 */
struct DnsRedirectEntry
{
    /**
     * @brief Hostname or IP address which is redirected.
     */
    std::wstring hostname;

    /**
     * @brief Address the name is redirected to.
     *
     * The address is an IPv4 or an IPv6 address literal: the sandbox answers
     * the name resolution of the packaged application with it and never asks
     * the host for the name.
     */
    std::wstring redirect;
};

/**
 * @brief Protocol of the proxy of the packaged application.
 *
 * The workspace offers SOCKS5 only; the enumeration exists because the type
 * travels with the project file as a token, so a file which names another
 * protocol is rejected instead of being read as SOCKS5.
 */
enum class ProxyType
{
    Socks5 ///< SOCKS5, the only protocol the workspace offers.
};

/**
 * @brief Get the token of a proxy protocol.
 * @param[in] type The proxy protocol.
 * @return The canonical lower case token, for example `"socks5"`.
 */
const char* ProxyTypeToken(ProxyType type);

/**
 * @brief Resolve a proxy protocol from its token.
 *
 * The comparison ignores the case and accepts spaces and dashes in place of
 * the underscore of the canonical token, like the tokens of the isolation
 * modes do. The tokens are ASCII, so they are handled as narrow text like the
 * JSON document which carries them.
 *
 * @param[in] token The token to resolve.
 * @param[out] out The resolved protocol when the token is known.
 * @return true when the token names a proxy protocol.
 */
bool ParseProxyTypeToken(std::string_view token, ProxyType& out);

/**
 * @brief Proxy configuration of the Network workspace.
 *
 * The proxy carries the TCP traffic, the UDP traffic or both through a SOCKS5
 * server: the two flags are the switch of the configuration, so at least one
 * of them has to be set before the server and its port are required.
 *
 * The configuration is kept as the user entered it, including the parts which
 * a disabled proxy does not use: unchecking both protocols keeps the server,
 * the port and the credentials, so the user can turn the proxy off without
 * losing what was typed.
 */
struct ProxyConfig
{
    /**
     * @brief Protocol of the proxy.
     */
    ProxyType type = ProxyType::Socks5;

    /**
     * @brief Whether the TCP traffic of the application is proxied.
     */
    bool tcp = false;

    /**
     * @brief Whether the UDP traffic of the application is proxied.
     */
    bool udp = false;

    /**
     * @brief Hostname or address of the proxy server, empty while unset.
     */
    std::wstring server;

    /**
     * @brief Port of the proxy server, empty while unset.
     *
     * The port is kept as text, like every other scalar of the model: an empty
     * text is the port which was not entered yet, and the validation of the
     * model is the only place which gives the text a meaning.
     */
    std::wstring port;

    /**
     * @brief Optional user name, empty while no authentication is configured.
     */
    std::wstring username;

    /**
     * @brief Optional password, empty while no authentication is configured.
     *
     * The password is stored as the user entered it: the sandbox has to send
     * it to the server, so there is nothing to mask here.
     */
    std::wstring password;
};

/**
 * @brief Editable content of the Network workspace of the packer.
 *
 * The model holds the DNS redirections and the proxy configuration the user
 * entered in the workspace. It holds no wxWidgets dependency and never touches
 * the network or the host filesystem, so its validation rules are unit
 * testable.
 *
 * The entries keep the order they were added in, so the table of the
 * workspace shows them the way the user entered them.
 *
 * Every operation which can fail validates its input first and reports an
 * English error description without changing the model.
 */
class NetworkModel
{
public:
    /**
     * @brief Drop every DNS redirection and the proxy configuration.
     *
     * The model is left in the state of a fresh session.
     */
    void Reset();

    /**
     * @brief Whether the model holds no DNS redirection and no proxy.
     * @return true when no entry was added and no proxy was configured.
     */
    bool IsEmpty() const;

    /**
     * @brief Get every DNS redirection the model holds.
     *
     * The entries are ordered the way they were added, so the rows of the
     * workspace table follow the order the user entered them in.
     *
     * @return The entries in insertion order.
     */
    const std::vector<DnsRedirectEntry>& DnsEntries() const;

    /**
     * @brief Append one DNS redirection.
     *
     * The call fails when the hostname or the redirect target is empty, when
     * either of them contains a whitespace character, when the redirect target
     * is not an IPv4 or an IPv6 address literal, or when the hostname is
     * already listed. The comparison of the hostnames ignores the case and a
     * trailing dot, because the name resolution of the host does as well.
     *
     * @param[in] hostname Hostname or IP address to redirect.
     * @param[in] redirect Address the name is redirected to.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddDnsEntry(const std::wstring& hostname, const std::wstring& redirect, std::string& error);

    /**
     * @brief Replace one DNS redirection.
     *
     * The entry keeps its position, so editing a row of the workspace table
     * does not reorder the table. An entry may be renamed to its own hostname
     * with a different case; a hostname which is listed by another entry is
     * refused.
     *
     * The call fails when the index does not name an entry, when the hostname
     * or the redirect target is empty, when either of them contains a
     * whitespace character, when the redirect target is not an IPv4 or an IPv6
     * address literal, or when the hostname is listed by another entry.
     *
     * @param[in] index Position of the entry to replace.
     * @param[in] hostname Hostname or IP address to redirect.
     * @param[in] redirect Address the name is redirected to.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool SetDnsEntry(std::size_t index, const std::wstring& hostname, const std::wstring& redirect, std::string& error);

    /**
     * @brief Drop one DNS redirection.
     * @param[in] index Position of the entry to drop.
     * @return true when the index named an entry and it was removed.
     */
    bool RemoveDnsEntry(std::size_t index);

    /**
     * @brief Find the entry of a hostname.
     * @param[in] hostname Hostname to look for, compared ignoring the case and
     *                     a trailing dot.
     * @return The position of the entry, -1 when the hostname is not listed.
     */
    std::ptrdiff_t IndexOfHostname(const std::wstring& hostname) const;

    /**
     * @brief Get the proxy configuration of the workspace.
     *
     * The configuration is returned as it was stored, including a disabled
     * proxy which still holds a server, a port or credentials.
     *
     * @return The proxy configuration of the model.
     */
    const ProxyConfig& Proxy() const;

    /**
     * @brief Whether a proxy was configured.
     *
     * A configuration counts as configured as soon as one of its protocols is
     * enabled or one of its fields carries a value, so a configuration which
     * was typed and then disabled is still part of the project file.
     *
     * @return true when the model holds a proxy configuration.
     */
    bool HasProxy() const;

    /**
     * @brief Replace the proxy configuration.
     *
     * The call validates the whole configuration first and reports an English
     * error description without changing the model on failure:
     *
     * - The server has to be free of whitespace characters while it carries a
     *   value.
     * - The port has to be empty or a decimal number between 1 and 65535
     *   without a leading zero.
     * - A protocol may be enabled only while both the server and the port
     *   carry a value, because an enabled proxy without a server is a
     *   configuration the sandbox could not use.
     *
     * The user name and the password are free text: they may be empty and may
     * carry whitespace characters, because a credential of a SOCKS5 server can
     * be spelled that way.
     *
     * @param[in] config The configuration to store.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool SetProxy(const ProxyConfig& config, std::string& error);

private:
    /**
     * @brief The DNS redirections in insertion order.
     */
    std::vector<DnsRedirectEntry> dns_entries_;

    /**
     * @brief The proxy configuration of the workspace.
     */
    ProxyConfig proxy_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_NETWORK_MODEL_HPP
