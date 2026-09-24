#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "WString.hpp"
#include "FsIsolationBuilder.hpp"

bool appbox::test::WriteFsIsolationFile(const appbox::LoaderConfig& config,
                                        const std::vector<FsIsolationEntry>& entries)
{
    nlohmann::json document;
    document[appbox::filesystem_isolation::kVersionKey] = appbox::filesystem_isolation::kVersion;
    document[appbox::filesystem_isolation::kEntriesKey] = nlohmann::json::array();

    for (const auto& entry : entries)
    {
        nlohmann::json item;
        item[appbox::filesystem_isolation::kPathKey] = appbox::WideToUTF8(entry.path);
        item[appbox::filesystem_isolation::kKindKey] = appbox::filesystem_isolation::EntryKindToken(entry.kind);
        item[appbox::filesystem_isolation::kIsolationKey] =
            appbox::filesystem_isolation::IsolationToken(entry.isolation);
        document[appbox::filesystem_isolation::kEntriesKey].push_back(std::move(item));
    }

    const auto path = std::filesystem::path(appbox::UTF8ToWide(config.overlay_fs)) / L"filesystem-isolation.json";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    const auto text = document.dump(2);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}
