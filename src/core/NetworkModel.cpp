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

} // namespace

namespace appbox
{

void NetworkModel::Reset()
{
    dns_entries_.clear();
}

bool NetworkModel::IsEmpty() const
{
    return dns_entries_.empty();
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

} // namespace appbox
