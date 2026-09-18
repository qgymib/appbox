#include <gtest/gtest.h>
#include "src/core/ZipReader.hpp"
#include "src/core/ZipWriter.hpp"
#include "WString.hpp"
#include <zip.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Generate a unique name fragment for temporary folders.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

/**
 * @brief RAII helper creating a unique folder below the temp directory.
 */
class TempDir
{
public:
    TempDir()
    {
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / (L"appbox-zipreader-" + UniqueFragment());
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    /**
     * @brief Get the folder path.
     * @return The folder path.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Read a whole file as text.
 * @param[in] path File path.
 * @return The file content, empty when the file cannot be read.
 */
std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return {};
    }

    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}

/**
 * @brief Create a zip archive with a single unsafe entry name.
 *
 * libzip refuses to create such an entry through the regular API, so the
 * archive is assembled by hand to model a hostile input archive.
 *
 * @param[in] path Archive path.
 * @param[in] entry Entry name to store.
 * @param[in] content Entry content.
 * @return true on success.
 */
bool MakeArchiveWithEntry(const std::filesystem::path& path, const char* entry, const char* content)
{
    const auto utf8_path = appbox::WideToUTF8(path.wstring());

    int error_code = 0;
    zip_t* archive = zip_open(utf8_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error_code);
    if (archive == nullptr)
    {
        return false;
    }

    zip_source_t* source = zip_source_buffer(archive, content, std::strlen(content), 0);
    if (source == nullptr || zip_file_add(archive, entry, source, ZIP_FL_ENC_UTF_8) < 0)
    {
        if (source != nullptr)
        {
            zip_source_free(source);
        }
        zip_discard(archive);
        return false;
    }

    if (zip_close(archive) < 0)
    {
        zip_discard(archive);
        return false;
    }
    return true;
}

} // namespace

TEST(ZipReader, ExtractsArchiveWrittenByZipWriter)
{
    TempDir temp;
    const auto archive_path = temp.Get() / L"round-trip.zip";
    const auto dest = temp.Get() / L"extracted";

    {
        appbox::ZipWriter writer(archive_path.wstring());
        std::string error;
        ASSERT_TRUE(writer.AddDirectory("filesystem", error)) << error;
        ASSERT_TRUE(writer.AddDirectory("filesystem/#ProgramFiles#", error)) << error;
        ASSERT_TRUE(writer.AddDirectory("filesystem/#ProgramFiles#/MyApp", error)) << error;
        ASSERT_TRUE(writer.AddDirectory("filesystem/#ProgramFiles#/MyApp/emptydir", error)) << error;
        ASSERT_TRUE(writer.AddFileBuffer("filesystem/#ProgramFiles#/MyApp/app.exe", "EXE", 3, error))
            << error;
        ASSERT_TRUE(writer.AddFileBuffer("filesystem/#ProgramFiles#/MyApp/data/config.txt", "CFG", 3,
                                         error))
            << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    const auto result = appbox::ExtractArchive(archive_path.wstring(), dest.wstring());
    EXPECT_EQ(result, "") << result;

    const auto root = dest / L"filesystem" / L"#ProgramFiles#" / L"MyApp";
    EXPECT_EQ(ReadFile(root / L"app.exe"), "EXE");
    EXPECT_EQ(ReadFile(root / L"data" / L"config.txt"), "CFG");
    EXPECT_TRUE(std::filesystem::is_directory(root / L"emptydir"));
}

TEST(ZipReader, CreatesTheDestinationFolder)
{
    TempDir temp;
    const auto archive_path = temp.Get() / L"dest.zip";
    const auto dest = temp.Get() / L"a" / L"b" / L"c";

    {
        appbox::ZipWriter writer(archive_path.wstring());
        std::string error;
        ASSERT_TRUE(writer.AddFileBuffer("note.txt", "NOTE", 4, error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    const auto result = appbox::ExtractArchive(archive_path.wstring(), dest.wstring());
    EXPECT_EQ(result, "") << result;
    EXPECT_EQ(ReadFile(dest / L"note.txt"), "NOTE");
}

TEST(ZipReader, ReportsMissingArchive)
{
    TempDir temp;
    const auto missing = temp.Get() / L"missing.zip";

    const auto result = appbox::ExtractArchive(missing.wstring(), (temp.Get() / L"out").wstring());
    EXPECT_FALSE(result.empty());
}

TEST(ZipReader, RejectsParentReferenceEntry)
{
    TempDir temp;
    const auto archive_path = temp.Get() / L"evil.zip";
    ASSERT_TRUE(MakeArchiveWithEntry(archive_path, "../escaped.txt", "EVIL"));

    const auto dest = temp.Get() / L"dest";
    const auto result = appbox::ExtractArchive(archive_path.wstring(), dest.wstring());

    EXPECT_FALSE(result.empty());
    EXPECT_FALSE(std::filesystem::exists(temp.Get() / L"escaped.txt"));
    EXPECT_FALSE(std::filesystem::exists(dest.parent_path() / L"escaped.txt"));
}

TEST(ZipReader, RejectsAbsoluteEntry)
{
    TempDir temp;
    const auto archive_path = temp.Get() / L"absolute.zip";
    ASSERT_TRUE(MakeArchiveWithEntry(archive_path, "/escaped.txt", "EVIL"));

    const auto dest = temp.Get() / L"dest";
    const auto result = appbox::ExtractArchive(archive_path.wstring(), dest.wstring());

    EXPECT_FALSE(result.empty());
    EXPECT_FALSE(std::filesystem::exists(dest / L"escaped.txt"));
}

TEST(ZipReader, OverwritesExistingFiles)
{
    TempDir temp;
    const auto archive_path = temp.Get() / L"overwrite.zip";
    const auto dest = temp.Get() / L"dest";

    {
        appbox::ZipWriter writer(archive_path.wstring());
        std::string error;
        ASSERT_TRUE(writer.AddFileBuffer("app/config.ini", "NEW", 3, error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    std::filesystem::create_directories(dest / L"app");
    {
        std::ofstream stale(dest / L"app" / L"config.ini", std::ios::binary);
        stale << "OLD";
    }

    const auto result = appbox::ExtractArchive(archive_path.wstring(), dest.wstring());
    EXPECT_EQ(result, "") << result;
    EXPECT_EQ(ReadFile(dest / L"app" / L"config.ini"), "NEW");
}

/**
 * @brief Extraction reports every file it writes, using the path below the
 *        layer tree of the archive.
 */
TEST(ZipReader, ReportsEveryExtractedFile)
{
    TempDir temp;
    const auto archive_path = temp.Get() / L"progress.zip";
    const auto dest = temp.Get() / L"progress-dest";

    {
        appbox::ZipWriter writer(archive_path.wstring());
        std::string error;
        ASSERT_TRUE(writer.AddDirectory("filesystem", error)) << error;
        ASSERT_TRUE(writer.AddDirectory("filesystem/#ProgramFiles#", error)) << error;
        ASSERT_TRUE(writer.AddDirectory("filesystem/#ProgramFiles#/MyApp", error)) << error;
        ASSERT_TRUE(writer.AddDirectory("filesystem/#ProgramFiles#/MyApp/data", error)) << error;
        ASSERT_TRUE(writer.AddFileBuffer("MyApp.exe", "LOADER", 6, error)) << error;
        ASSERT_TRUE(writer.AddFileBuffer("MyApp.exe.json", "{}", 2, error)) << error;
        ASSERT_TRUE(writer.AddFileBuffer("filesystem/#ProgramFiles#/MyApp/app.exe", "EXE", 3, error))
            << error;
        ASSERT_TRUE(writer.AddFileBuffer("filesystem/#ProgramFiles#/MyApp/data/config.txt", "CFG", 3,
                                         error))
            << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    std::vector<appbox::BuildProgress> reports;
    const auto progress = [&reports](const appbox::BuildProgress& report) {
        reports.emplace_back(report);
        return true;
    };

    const auto result = appbox::ExtractArchive(archive_path.wstring(), dest.wstring(), progress);
    EXPECT_EQ(result, "") << result;

    ASSERT_FALSE(reports.empty());

    /* The run opens with the stage report before the first entry is written. */
    EXPECT_EQ(reports.front().stage, appbox::BuildStage::Extracting);
    EXPECT_EQ(reports.front().done, static_cast<std::size_t>(0));
    EXPECT_EQ(reports.front().total, static_cast<std::size_t>(4));
    EXPECT_TRUE(reports.front().current.empty());

    /*
     * Directory entries only recreate the folder structure: the total covers
     * the two entries of the archive root plus the two content files.
     */
    std::set<std::wstring> named;
    std::size_t seen = 0;
    for (const auto& report : reports)
    {
        EXPECT_EQ(report.stage, appbox::BuildStage::Extracting);
        EXPECT_EQ(report.total, static_cast<std::size_t>(4));
        EXPECT_GE(report.done, seen);
        seen = report.done;

        if (!report.current.empty())
        {
            named.insert(report.current);
        }
    }

    /* The layer prefix is dropped, the entries of the archive root keep theirs. */
    EXPECT_EQ(named.count(L"MyApp.exe"), static_cast<std::size_t>(1));
    EXPECT_EQ(named.count(L"MyApp.exe.json"), static_cast<std::size_t>(1));
    EXPECT_EQ(named.count(L"MyApp\\app.exe"), static_cast<std::size_t>(1));
    EXPECT_EQ(named.count(L"MyApp\\data\\config.txt"), static_cast<std::size_t>(1));
}

/**
 * @brief A cancelled extraction stops before it writes the pending entry.
 */
TEST(ZipReader, ExtractCanBeCancelled)
{
    TempDir temp;
    const auto archive_path = temp.Get() / L"cancel.zip";
    const auto dest = temp.Get() / L"cancel-dest";

    {
        appbox::ZipWriter writer(archive_path.wstring());
        std::string error;
        ASSERT_TRUE(writer.AddFileBuffer("note.txt", "NOTE", 4, error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    const auto result = appbox::ExtractArchive(archive_path.wstring(), dest.wstring(),
                                               [](const appbox::BuildProgress&) { return false; });
    EXPECT_EQ(result, appbox::kBuildCancelledError);
    EXPECT_FALSE(std::filesystem::exists(dest / L"note.txt"));
}
