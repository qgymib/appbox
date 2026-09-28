#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include "FsIsolationBuilder.hpp"

/**
 * @brief Path of the filesystem isolation file of a case.
 * @param[in] case_root Root directory of the case.
 * @return The path of the file.
 */
static std::filesystem::path IsolationFilePath(const std::filesystem::path& case_root)
{
    return case_root / appbox::layout::kAppDirNameW / appbox::layout::kFilesystemDirNameW /
           appbox::layout::kIsolationFileNameW;
}

/**
 * @brief Write the text of the filesystem isolation file of a case.
 * @param[in] case_root Root directory of the case.
 * @param[in] text Text to write.
 * @return true on success.
 */
static bool WriteIsolationText(const std::filesystem::path& case_root, const std::string& text)
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

bool appbox::test::WriteFsIsolationFile(const std::filesystem::path&         case_root,
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

    return WriteIsolationText(case_root, document.dump(2));
}

bool appbox::test::WriteRawFsIsolationFile(const std::filesystem::path& case_root, const std::string& text)
{
    return WriteIsolationText(case_root, text);
}
