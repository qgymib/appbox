#include "ProxyConfig.hpp"
#include "NetworkIsolation.hpp"
#include <nlohmann/json.hpp>
#include <exception>

namespace
{

/**
 * @brief Read a member of the proxy which holds a flag.
 *
 * A member which is missing or which is not a flag leaves the flag clear: the
 * sandbox only carries the traffic the file asks for.
 *
 * @param[in] proxy Object of the proxy.
 * @param[in] member Name of the member.
 * @return The value of the member, false when it is missing or not a flag.
 */
bool ReadFlag(const nlohmann::json& proxy, const char* member)
{
    const auto it = proxy.find(member);
    if (it == proxy.end() || !it->is_boolean())
    {
        return false;
    }
    return it->get<bool>();
}

/**
 * @brief Read a member of the proxy which holds a text.
 * @param[in] proxy Object of the proxy.
 * @param[in] member Name of the member.
 * @param[out] out Text of the member, untouched when it is missing.
 * @return true when the member is a text.
 */
bool ReadText(const nlohmann::json& proxy, const char* member, std::string& out)
{
    const auto it = proxy.find(member);
    if (it == proxy.end() || !it->is_string())
    {
        return false;
    }

    out = it->get<std::string>();
    return true;
}

/**
 * @brief Read the proxy of a document of the supported version.
 *
 * A proxy which cannot be used is reported as a disabled one instead of an
 * error, because the sandbox behaves the same way without it.
 *
 * @param[in] document Document of the network isolation file.
 * @return The proxy of the document, disabled when it holds none.
 */
appbox::network::ProxyConfig ReadProxy(const nlohmann::json& document)
{
    using appbox::network_isolation::kProxyKey;
    using appbox::network_isolation::kProxyPasswordKey;
    using appbox::network_isolation::kProxyPortKey;
    using appbox::network_isolation::kProxyServerKey;
    using appbox::network_isolation::kProxyTcpKey;
    using appbox::network_isolation::kProxyTypeKey;
    using appbox::network_isolation::kProxyUdpKey;
    using appbox::network_isolation::kProxyUsernameKey;

    const auto member = document.find(kProxyKey);
    if (member == document.end() || !member->is_object())
    {
        return {};
    }

    std::string type;
    if (!ReadText(*member, kProxyTypeKey, type) || !appbox::network_isolation::IsSocks5Token(type))
    {
        return {};
    }

    appbox::network::ProxyConfig config;
    config.tcp = ReadFlag(*member, kProxyTcpKey);
    config.udp = ReadFlag(*member, kProxyUdpKey);

    if (!ReadText(*member, kProxyServerKey, config.server) || config.server.empty())
    {
        return {};
    }

    std::string port;
    if (!ReadText(*member, kProxyPortKey, port))
    {
        return {};
    }
    if (appbox::network_isolation::ReadPortText(port, config.port) != appbox::network_isolation::PortText::Ok)
    {
        return {};
    }

    /* The credentials are optional; a member which is not a text is absent. */
    ReadText(*member, kProxyUsernameKey, config.username);
    ReadText(*member, kProxyPasswordKey, config.password);

    return config;
}

} // namespace

bool appbox::network::ParseProxyConfig(const std::string& text, ProxyConfig& out)
{
    out = ProxyConfig{};

    try
    {
        const auto document = nlohmann::json::parse(text);
        if (!document.is_object())
        {
            return false;
        }
        if (document.value(network_isolation::kVersionKey, 0) != network_isolation::kVersion)
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
