#include "ProxyConfig.hpp"
#include "NetworkIsolation.hpp"
#include <nlohmann/json.hpp>

namespace
{

/**
 * @brief Read the proxy of a document of the supported version.
 *
 * A proxy which cannot be used is reported as a disabled one instead of an
 * error, because the sandbox behaves the same way without it: the document
 * itself is accepted, so the DNS redirections it carries are applied either
 * way.
 *
 * @param[in] document The network isolation file.
 * @return The proxy of the document, disabled when it holds none.
 */
appbox::network::ProxyConfig ReadProxy(const appbox::network_isolation::Document& document)
{
    using appbox::network_isolation::IsSocks5Token;

    if (!document.proxy.has_value())
    {
        return {};
    }

    const appbox::network_isolation::Proxy& proxy = *document.proxy;

    if (!IsSocks5Token(proxy.type))
    {
        return {};
    }

    appbox::network::ProxyConfig config;
    config.tcp = proxy.tcp;
    config.udp = proxy.udp;

    if (proxy.server.empty())
    {
        return {};
    }
    config.server = proxy.server;

    const auto port = appbox::network_isolation::ReadPortText(proxy.port);
    if (!port)
    {
        return {};
    }
    config.port = *port;

    /* The credentials are optional; a member which carries no text is absent. */
    config.username = proxy.username;
    config.password = proxy.password;

    return config;
}

} // namespace

bool appbox::network::ParseProxyConfig(const std::string& text, ProxyConfig& out)
{
    out = ProxyConfig{};

    /*
     * The document is read as the structure of its schema
     * (`common/NetworkIsolation.hpp`) and never member by member. The proxy
     * member of the structure is read leniently, so a member of another type
     * leaves the default of the proxy instead of failing the document. A text
     * which is no document of the schema describes a session without a proxy,
     * which is the behaviour of a sandbox without a network isolation file.
     */
    const auto document = network_isolation::ParseDocument(text);
    if (!document)
    {
        return false;
    }

    out = ReadProxy(*document);
    return true;
}

std::string appbox::network::DescribeProxy(const ProxyConfig& config)
{
    if (!config.IsEnabled())
    {
        return "none";
    }

    std::string text = network_isolation::kSocks5Token;
    text += ' ';
    if (config.tcp)
    {
        text += "tcp";
    }
    if (config.tcp && config.udp)
    {
        text += '+';
    }
    if (config.udp)
    {
        text += "udp";
    }

    text += ' ';
    text += config.server;
    text += ':';
    text += std::to_string(static_cast<unsigned int>(config.port));

    if (config.HasCredentials())
    {
        text += " (credentials)";
    }
    return text;
}
