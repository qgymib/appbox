#include "ProxyConfig.hpp"
#include "NetworkIsolation.hpp"
#include <nlohmann/json.hpp>
#include <exception>

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
    using appbox::network_isolation::PortText;
    using appbox::network_isolation::ReadPortText;

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

    if (ReadPortText(proxy.port, config.port) != PortText::Ok)
    {
        return {};
    }

    /* The credentials are optional; a member which carries no text is absent. */
    config.username = proxy.username;
    config.password = proxy.password;

    return config;
}

} // namespace

bool appbox::network::ParseProxyConfig(const std::string& text, ProxyConfig& out)
{
    out = ProxyConfig{};

    try
    {
        /*
         * The document is read as the structure of its schema
         * (`common/NetworkIsolation.hpp`) and never member by member. The proxy
         * member of the structure is read leniently, so a member of another
         * type leaves the default of the proxy instead of failing the document.
         */
        const auto document = nlohmann::json::parse(text).get<network_isolation::Document>();
        if (document.version != network_isolation::kVersion)
        {
            return false;
        }

        out = ReadProxy(document);
        return true;
    }
    catch (const std::exception&)
    {
        out = ProxyConfig{};
        return false;
    }
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
