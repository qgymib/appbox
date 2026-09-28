#include "NetworkModel.hpp"
#include "NetworkIsolation.hpp"
#include "WString.hpp"
#include <cwctype>
#include <utility>

namespace
{

/**
 * @brief Whether a text holds a whitespace character.
 * @param[in] text The text to inspect.
 * @return true when the text holds at least one whitespace character.
 */
bool HasWhitespace(const std::wstring& text)
{
    for (const wchar_t character : text)
    {
        if (std::iswspace(static_cast<std::wint_t>(character)) != 0)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Quote a wide text for an English error description.
 * @param[in] text The text to quote.
 * @return The quoted UTF-8 text.
 */
std::string Quote(const std::wstring& text)
{
    return "'" + appbox::WideToUTF8(text) + "'";
}

/**
 * @brief Validate one field of a DNS redirection.
 *
 * The rules of the hostname and of the redirect target are the same, so both
 * are checked in a single place and the error names the field which failed.
 *
 * @param[in] value The value to check.
 * @param[in] field Name of the field as it appears in the error description.
 * @param[out] error Error description on failure.
 * @return true when the value may be stored.
 */
bool ValidateField(const std::wstring& value, const char* field, std::string& error)
{
    if (value.empty())
    {
        error = std::string("the ") + field + " must not be empty";
        return false;
    }
    if (HasWhitespace(value))
    {
        error = std::string("the ") + field + " " + Quote(value) + " contains a whitespace character";
        return false;
    }
    return true;
}

/**
 * @brief Validate the redirect target of a DNS redirection.
 *
 * The target is the address a redirected name resolves to inside the sandbox,
 * so it has to be an IPv4 or an IPv6 address literal: the sandbox answers the
 * name resolution of the application with it and never asks the host.
 *
 * @param[in] value The value to check.
 * @param[out] error Error description on failure.
 * @return true when the value may be stored.
 */
bool ValidateRedirect(const std::wstring& value, std::string& error)
{
    if (!ValidateField(value, "redirect target", error))
    {
        return false;
    }

    appbox::network_isolation::Address address;
    if (!appbox::network_isolation::ParseAddress(appbox::WideToUTF8(value), address))
    {
        error = "the redirect target " + Quote(value) + " is not an IPv4 or an IPv6 address";
        return false;
    }
    return true;
}

/**
 * @brief Validate the server of a proxy configuration.
 *
 * An empty server means the server was not entered yet; a server which carries
 * a value has to be free of whitespace characters, because a whitespace
 * character can neither be part of a hostname nor of an address literal.
 *
 * @param[in] value The value to check.
 * @param[out] error Error description on failure.
 * @return true when the value may be stored.
 */
bool ValidateProxyServer(const std::wstring& value, std::string& error)
{
    if (value.empty())
    {
        return true;
    }
    if (HasWhitespace(value))
    {
        error = "the proxy server " + Quote(value) + " contains a whitespace character";
        return false;
    }
    return true;
}

/**
 * @brief Validate the port of a proxy configuration.
 *
 * An empty port means the port was not entered yet; a port which carries a
 * value has to be a decimal number between 1 and 65535 without a leading zero,
 * so the text the user entered is never silently read as another port. The
 * rules are the ones of `network_isolation::ReadPortText`, which the sandbox
 * reads the port of the isolation file with, so the two sides cannot drift
 * apart; only the wording of the report is local to the workspace.
 *
 * @param[in] value The value to check.
 * @param[out] error Error description on failure.
 * @return true when the value may be stored.
 */
bool ValidateProxyPort(const std::wstring& value, std::string& error)
{
    std::uint16_t                             port = 0;
    const appbox::network_isolation::PortText outcome =
        appbox::network_isolation::ReadPortText(appbox::WideToUTF8(value), port);

    if (outcome == appbox::network_isolation::PortText::Ok || outcome == appbox::network_isolation::PortText::Empty)
    {
        return true;
    }
    if (outcome == appbox::network_isolation::PortText::NotDecimal)
    {
        error = "the proxy port " + Quote(value) + " is not a decimal number";
        return false;
    }
    if (outcome == appbox::network_isolation::PortText::LeadingZero)
    {
        error = "the proxy port " + Quote(value) + " has a leading zero";
        return false;
    }

    error = "the proxy port " + Quote(value) + " is not between 1 and 65535";
    return false;
}

} // namespace

namespace appbox
{

const char* ProxyTypeToken(ProxyType type)
{
    switch (type)
    {
    case ProxyType::Socks5:
        return "socks5";
    }
    return "socks5";
}

bool ParseProxyTypeToken(std::string_view token, ProxyType& out)
{
    if (!network_isolation::IsSocks5Token(token))
    {
        return false;
    }

    out = ProxyType::Socks5;
    return true;
}

void NetworkModel::Reset()
{
    dns_entries_.clear();
    proxy_ = ProxyConfig{};
}

bool NetworkModel::IsEmpty() const
{
    return dns_entries_.empty() && !HasProxy();
}

const std::vector<DnsRedirectEntry>& NetworkModel::DnsEntries() const
{
    return dns_entries_;
}

bool NetworkModel::AddDnsEntry(const std::wstring& hostname, const std::wstring& redirect, std::string& error)
{
    if (!ValidateField(hostname, "hostname", error) || !ValidateRedirect(redirect, error))
    {
        return false;
    }
    if (IndexOfHostname(hostname) >= 0)
    {
        error = "the hostname " + Quote(hostname) + " is listed twice";
        return false;
    }

    DnsRedirectEntry entry;
    entry.hostname = hostname;
    entry.redirect = redirect;
    dns_entries_.push_back(std::move(entry));
    return true;
}

bool NetworkModel::SetDnsEntry(std::size_t index, const std::wstring& hostname, const std::wstring& redirect,
                               std::string& error)
{
    if (index >= dns_entries_.size())
    {
        error = "the index " + std::to_string(index) + " does not name a DNS redirection";
        return false;
    }
    if (!ValidateField(hostname, "hostname", error) || !ValidateRedirect(redirect, error))
    {
        return false;
    }

    const auto other = IndexOfHostname(hostname);
    if (other >= 0 && static_cast<std::size_t>(other) != index)
    {
        error = "the hostname " + Quote(hostname) + " is listed twice";
        return false;
    }

    auto& entry = dns_entries_[index];
    entry.hostname = hostname;
    entry.redirect = redirect;
    return true;
}

bool NetworkModel::RemoveDnsEntry(std::size_t index)
{
    if (index >= dns_entries_.size())
    {
        return false;
    }

    dns_entries_.erase(dns_entries_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::ptrdiff_t NetworkModel::IndexOfHostname(const std::wstring& hostname) const
{
    for (std::size_t index = 0; index < dns_entries_.size(); ++index)
    {
        if (network_isolation::HostnamesEqual(WideToUTF8(dns_entries_[index].hostname), WideToUTF8(hostname)))
        {
            return static_cast<std::ptrdiff_t>(index);
        }
    }
    return -1;
}

const ProxyConfig& NetworkModel::Proxy() const
{
    return proxy_;
}

bool NetworkModel::HasProxy() const
{
    return proxy_.tcp || proxy_.udp || !proxy_.server.empty() || !proxy_.port.empty() || !proxy_.username.empty() ||
           !proxy_.password.empty();
}

bool NetworkModel::SetProxy(const ProxyConfig& config, std::string& error)
{
    if (!ValidateProxyServer(config.server, error) || !ValidateProxyPort(config.port, error))
    {
        return false;
    }

    if (config.tcp || config.udp)
    {
        if (config.server.empty())
        {
            error = "the proxy server must not be empty while TCP or UDP traffic is proxied";
            return false;
        }
        if (config.port.empty())
        {
            error = "the proxy port must not be empty while TCP or UDP traffic is proxied";
            return false;
        }
    }

    proxy_ = config;
    return true;
}

} // namespace appbox
