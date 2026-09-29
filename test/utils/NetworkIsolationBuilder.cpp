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
    /*
     * The document is filled as the structure of the schema of the file, so a
     * case writes the same document the workspace writes.
     */
    appbox::network_isolation::Document document;

    for (const auto& entry : entries)
    {
        appbox::network_isolation::Entry item;
        item.hostname = appbox::WideToUTF8(entry.hostname);
        item.redirect = appbox::WideToUTF8(entry.redirect);
        document.entries.push_back(std::move(item));
    }

    if (proxy.IsConfigured())
    {
        appbox::network_isolation::Proxy item;
        item.type = appbox::network_isolation::kSocks5Token;
        item.tcp = proxy.tcp;
        item.udp = proxy.udp;
        item.server = appbox::WideToUTF8(proxy.server);
        item.port = appbox::WideToUTF8(proxy.port);
        item.username = appbox::WideToUTF8(proxy.username);
        item.password = appbox::WideToUTF8(proxy.password);
        document.proxy = std::move(item);
    }

    return WriteText(case_root, nlohmann::json(document).dump(2));
}

bool appbox::test::WriteNetworkIsolationFileText(const std::filesystem::path& case_root, const std::string& text)
{
    return WriteText(case_root, text);
}
