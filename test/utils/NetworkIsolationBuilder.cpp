#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include "NetworkIsolationBuilder.hpp"

namespace
{

/**
 * @brief Path of the network isolation file of a test sandbox.
 * @param[in] case_root Root directory of the case.
 * @return The path of the file inside the resources of the case.
 */
std::filesystem::path IsolationFilePath(const std::filesystem::path& case_root)
{
    return case_root / appbox::layout::kAppDirNameW / appbox::layout::kNetworkDirNameW /
           appbox::layout::kIsolationFileNameW;
}

/**
 * @brief Write a text as the network isolation file of a test sandbox.
 * @param[in] case_root Root directory of the case.
 * @param[in] text Text of the file.
 * @return true on success.
 */
bool WriteText(const std::filesystem::path& case_root, const std::string& text)
{
    const auto path = IsolationFilePath(case_root);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}

} // namespace

bool appbox::test::WriteNetworkIsolationFile(const std::filesystem::path&              case_root,
                                             const std::vector<NetworkIsolationEntry>& entries)
{
    return WriteNetworkIsolationFile(case_root, entries, NetworkIsolationProxy{});
}

bool appbox::test::WriteNetworkIsolationFile(const std::filesystem::path&              case_root,
                                             const std::vector<NetworkIsolationEntry>& entries,
                                             const NetworkIsolationProxy&              proxy)
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

    if (proxy.IsConfigured())
    {
        nlohmann::json item;
        item[appbox::network_isolation::kProxyTypeKey] = appbox::network_isolation::kSocks5Token;
        item[appbox::network_isolation::kProxyTcpKey] = proxy.tcp;
        item[appbox::network_isolation::kProxyUdpKey] = proxy.udp;
        item[appbox::network_isolation::kProxyServerKey] = appbox::WideToUTF8(proxy.server);
        item[appbox::network_isolation::kProxyPortKey] = appbox::WideToUTF8(proxy.port);
        item[appbox::network_isolation::kProxyUsernameKey] = appbox::WideToUTF8(proxy.username);
        item[appbox::network_isolation::kProxyPasswordKey] = appbox::WideToUTF8(proxy.password);
        document[appbox::network_isolation::kProxyKey] = std::move(item);
    }

    return WriteText(case_root, document.dump(2));
}

bool appbox::test::WriteNetworkIsolationFileText(const std::filesystem::path& case_root, const std::string& text)
{
    return WriteText(case_root, text);
}
