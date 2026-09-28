#include "DnsTable.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

namespace
{

/**
 * @brief Read a member of an entry which holds a string.
 * @param[in] entry Object of the entry.
 * @param[in] member Name of the member.
 * @param[out] out Text of the member.
 * @param[out] error Error description on failure.
 * @return true when the member is a string.
 */
bool ReadString(const nlohmann::json& entry, const char* member, std::string& out, std::string& error)
{
    const auto it = entry.find(member);
    if (it == entry.end())
    {
        error = std::string("a network isolation file entry has no '") + member + "' member";
        return false;
    }

    if (!it->is_string())
    {
        error = std::string("the '") + member + "' member of a network isolation file entry is not a string";
        return false;
    }

    out = it->get<std::string>();
    return true;
}

/**
 * @brief Whether a redirect address fits the family of a question.
 * @param[in] family Family of the redirect address.
 * @param[in] requested Family the application asked for.
 * @return true when the address can answer the question.
 */
bool FamilyMatches(appbox::network_isolation::AddressFamily family, appbox::network::RequestedFamily requested)
{
    if (requested == appbox::network::RequestedFamily::Any)
    {
        return true;
    }

    const bool wants_ipv6 = requested == appbox::network::RequestedFamily::IPv6;
    const bool is_ipv6 = family == appbox::network_isolation::AddressFamily::IPv6;
    return wants_ipv6 == is_ipv6;
}

} // namespace

bool appbox::network::DnsTable::Parse(const std::string& text, std::string& error)
{
    std::vector<DnsEntry> entries;

    try
    {
        const auto document = nlohmann::json::parse(text);
        if (!document.is_object())
        {
            error = "the network isolation file is not a JSON object";
            return false;
        }

        if (document.value(network_isolation::kVersionKey, 0) != network_isolation::kVersion)
        {
            error = "unsupported network isolation file version";
            return false;
        }

        const auto list = document.find(network_isolation::kEntriesKey);
        if (list != document.end())
        {
            if (!list->is_array())
            {
                error = std::string("the '") + network_isolation::kEntriesKey +
                        "' member of the network isolation file is not a list";
                return false;
            }

            for (const auto& item : *list)
            {
                if (!item.is_object())
                {
                    error = "a network isolation file entry is not an object";
                    return false;
                }

                DnsEntry entry;
                if (!ReadString(item, network_isolation::kHostnameKey, entry.hostname, error) ||
                    !ReadString(item, network_isolation::kRedirectKey, entry.redirect, error))
                {
                    return false;
                }

                if (entry.hostname.empty())
                {
                    error = "a network isolation file entry has an empty hostname";
                    return false;
                }

                network_isolation::Address address;
                if (!network_isolation::ParseAddress(entry.redirect, address))
                {
                    error = "the redirect '" + entry.redirect +
                            "' of a network isolation file entry is not an IPv4 or an IPv6 address";
                    return false;
                }
                entry.family = address.family;
                entry.hostname = network_isolation::NormalizeHostname(entry.hostname);

                /*
                 * A hostname which is listed twice keeps the last entry, so a
                 * hand written file can correct an entry by repeating it.
                 */
                bool replaced = false;
                for (auto& existing : entries)
                {
                    if (existing.hostname == entry.hostname)
                    {
                        existing = entry;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced)
                {
                    entries.push_back(std::move(entry));
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        error = std::string("the network isolation file is not valid: ") + e.what();
        return false;
    }

    entries_ = std::move(entries);
    return true;
}

const appbox::network::DnsEntry* appbox::network::DnsTable::Find(const std::string& hostname,
                                                                 RequestedFamily    family) const
{
    const std::string normalized = network_isolation::NormalizeHostname(hostname);

    for (const auto& entry : entries_)
    {
        if (entry.hostname == normalized && FamilyMatches(entry.family, family))
        {
            return &entry;
        }
    }
    return nullptr;
}

std::size_t appbox::network::DnsTable::Count() const
{
    return entries_.size();
}

void appbox::network::DnsTable::Clear()
{
    entries_.clear();
}
