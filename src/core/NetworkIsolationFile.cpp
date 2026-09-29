#include "NetworkIsolationFile.hpp"
#include "NetworkIsolation.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

bool appbox::BuildNetworkIsolationFile(const NetworkModel& model, std::string& text, std::string& error)
{
    error.clear();
    text.clear();

    try
    {
        /*
         * The document is built as the structure of the schema
         * (`common/NetworkIsolation.hpp`) and not as a JSON object, so the text
         * the packer writes and the text the sandbox reads are described by one
         * definition.
         */
        network_isolation::Document document;

        for (const auto& entry : model.DnsEntries())
        {
            network_isolation::Entry item;
            item.hostname = WideToUTF8(entry.hostname);
            item.redirect = WideToUTF8(entry.redirect);
            document.entries.push_back(std::move(item));
        }

        if (model.HasProxy())
        {
            const ProxyConfig& proxy = model.Proxy();

            network_isolation::Proxy item;
            item.type = ProxyTypeToken(proxy.type);
            item.tcp = proxy.tcp;
            item.udp = proxy.udp;
            item.server = WideToUTF8(proxy.server);
            item.port = WideToUTF8(proxy.port);
            item.username = WideToUTF8(proxy.username);
            item.password = WideToUTF8(proxy.password);
            document.proxy = std::move(item);
        }

        text = nlohmann::json(document).dump(2);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to build the network isolation file: ") + e.what();
        return false;
    }
}
