#include "DnsTable.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

namespace
{

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
        /*
         * The document is read as the structure of its schema
         * (`common/NetworkIsolation.hpp`) and never member by member, so a
         * member which is missing, which is of another type or which carries an
         * empty hostname is refused while the file is read.
         */
        const auto document = nlohmann::json::parse(text).get<network_isolation::Document>();

        if (document.version != network_isolation::kVersion)
        {
            error = "unsupported network isolation file version";
            return false;
        }

        for (const auto& item : document.entries)
        {
            DnsEntry entry;
            entry.hostname = network_isolation::NormalizeHostname(item.hostname);
            entry.redirect = item.redirect;

            network_isolation::Address address;
            if (!network_isolation::ParseAddress(entry.redirect, address))
            {
                error = "the redirect '" + entry.redirect +
                        "' of a network isolation file entry is not an IPv4 or an IPv6 address";
                return false;
            }
            entry.family = address.family;

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
    catch (const IsolationDocumentError& e)
    {
        error = e.what();
        return false;
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
