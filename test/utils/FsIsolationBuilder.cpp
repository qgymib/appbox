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
    return WriteIsolationText(case_root, BuildFsIsolationText(entries));
}

std::string appbox::test::BuildFsIsolationText(const std::vector<FsIsolationEntry>& entries)
{
    /*
     * The document is filled as the structure of the schema of the file, so a
     * case writes the same document the workspace writes.
     */
    appbox::filesystem_isolation::Document document;

    for (const auto& entry : entries)
    {
        appbox::filesystem_isolation::Entry item;
        item.path = appbox::WideToUTF8(entry.path);
        item.kind = entry.kind;
        item.isolation = entry.isolation;
        document.entries.push_back(std::move(item));
    }

    return nlohmann::json(document).dump(2);
}

bool appbox::test::WriteRawFsIsolationFile(const std::filesystem::path& case_root, const std::string& text)
{
    return WriteIsolationText(case_root, text);
}
