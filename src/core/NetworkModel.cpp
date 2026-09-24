#include "NetworkModel.hpp"
#include "WString.hpp"
#include <cwctype>
#include <utility>

namespace
{

/**
 * @brief Compare two texts ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when both texts are equal ignoring the case.
 */
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towlower(left[index]) != std::towlower(right[index]))
        {
            return false;
        }
    }
    return true;
}

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
    if (!ValidateField(hostname, "hostname", error) || !ValidateField(redirect, "redirect target", error))
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
    if (!ValidateField(hostname, "hostname", error) || !ValidateField(redirect, "redirect target", error))
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
        if (EqualsIgnoreCase(dns_entries_[index].hostname, hostname))
        {
            return static_cast<std::ptrdiff_t>(index);
        }
    }
    return -1;
}

} // namespace appbox
