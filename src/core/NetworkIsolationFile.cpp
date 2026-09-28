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
        nlohmann::json document;
        document[network_isolation::kVersionKey] = network_isolation::kVersion;
        document[network_isolation::kEntriesKey] = nlohmann::json::array();

        for (const auto& entry : model.DnsEntries())
        {
            nlohmann::json item;
            item[network_isolation::kHostnameKey] = WideToUTF8(entry.hostname);
            item[network_isolation::kRedirectKey] = WideToUTF8(entry.redirect);
            document[network_isolation::kEntriesKey].push_back(std::move(item));
        }

        if (model.HasProxy())
        {
            const ProxyConfig& proxy = model.Proxy();

            nlohmann::json item;
            item[network_isolation::kProxyTypeKey] = ProxyTypeToken(proxy.type);
            item[network_isolation::kProxyTcpKey] = proxy.tcp;
            item[network_isolation::kProxyUdpKey] = proxy.udp;
            item[network_isolation::kProxyServerKey] = WideToUTF8(proxy.server);
            item[network_isolation::kProxyPortKey] = WideToUTF8(proxy.port);
            item[network_isolation::kProxyUsernameKey] = WideToUTF8(proxy.username);
            item[network_isolation::kProxyPasswordKey] = WideToUTF8(proxy.password);
            document[network_isolation::kProxyKey] = std::move(item);
        }

        text = document.dump(2);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to build the network isolation file: ") + e.what();
        return false;
    }
}
