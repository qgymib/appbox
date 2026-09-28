#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "WString.hpp"
#include "NetworkIsolationBuilder.hpp"

bool appbox::test::WriteNetworkIsolationFile(const appbox::LoaderConfig&               config,
                                             const std::vector<NetworkIsolationEntry>& entries)
{
    nlohmann::json document;
    document[appbox::network_isolation::kVersionKey] = appbox::network_isolation::kVersion;
    document[appbox::network_isolation::kEntriesKey] = nlohmann::json::array();

    for (const auto& entry : entries)
    {
        nlohmann::json item;
        item[appbox::network_isolation::kHostnameKey] = appbox::WideToUTF8(entry.hostname);
        item[appbox::network_isolation::kRedirectKey] = appbox::WideToUTF8(entry.redirect);
        document[appbox::network_isolation::kEntriesKey].push_back(std::move(item));
    }

    const auto    path = std::filesystem::path(appbox::UTF8ToWide(config.overlay_fs)) / L"network-isolation.json";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    const auto text = document.dump(2);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}
