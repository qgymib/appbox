#include <gtest/gtest.h>
#include "src/core/ZipWriter.hpp"
#include "WString.hpp"
#include <zip.h>
#include <chrono>
#include <filesystem>
#include <set>
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
        path_ = base / (L"appbox-zipwriter-" + UniqueFragment());
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
 * @brief Read the content of one zip entry.
 * @param[in] archive Open zip archive.
 * @param[in] name Entry name.
 * @return The entry content, empty when the entry cannot be read.
 */
std::string ReadEntry(zip_t* archive, const char* name)
{
    const auto index = zip_name_locate(archive, name, 0);
    if (index < 0)
    {
        return {};
    }

    zip_stat_t stat = {};
    if (zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &stat) < 0)
    {
        return {};
    }

    std::string content(stat.size, '\0');
    zip_file_t* file = zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0);
    if (file == nullptr)
    {
        return {};
    }
    const auto read = zip_fread(file, content.data(), stat.size);
    zip_fclose(file);
    if (read != static_cast<zip_int64_t>(stat.size))
    {
        return {};
    }
    return content;
}

/**
 * @brief Collect the entry names of a zip archive.
 * @param[in] archive Open zip archive.
 * @return The set of entry names as stored.
 */
std::set<std::string> EntryNames(zip_t* archive)
{
    std::set<std::string> names;
    const auto count = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < count; ++i)
    {
        const char* name = zip_get_name(archive, static_cast<zip_uint64_t>(i), 0);
        if (name != nullptr)
        {
            names.insert(name);
        }
    }
    return names;
}

} // namespace

TEST(ZipWriter, WritesEntriesReadableByLibzip)
{
    TempDir temp;
    const auto zip_path = temp.Get() / L"archive.zip";

    std::filesystem::path disk_file;
    {
        const auto disk_path = temp.Get() / L"payload.bin";
        FILE* handle = nullptr;
        ASSERT_EQ(_wfopen_s(&handle, disk_path.wstring().c_str(), L"wb"), 0);
        ASSERT_NE(handle, nullptr);
        fwrite("DISK", 1, 4, handle);
        fclose(handle);
        disk_file = disk_path;

        appbox::ZipWriter writer(zip_path.wstring());
        std::string error;
        ASSERT_TRUE(writer.AddDirectory("folder", error)) << error;
        ASSERT_TRUE(writer.AddDirectory("folder/sub", error)) << error;
        ASSERT_TRUE(writer.AddFileBuffer("folder/sub/file.txt", "hello", 5, error)) << error;
        ASSERT_TRUE(writer.AddFileDisk(disk_path.wstring(), "root.bin", error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    int error_code = 0;
    zip_t* archive = zip_open(appbox::WideToUTF8(zip_path.wstring()).c_str(), ZIP_RDONLY, &error_code);
    ASSERT_NE(archive, nullptr);

    const auto names = EntryNames(archive);
    EXPECT_EQ(names.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(ReadEntry(archive, "folder/sub/file.txt"), "hello");
    EXPECT_EQ(ReadEntry(archive, "root.bin"), "DISK");

    /* Directory entries keep a trailing slash so extractors recreate them. */
    EXPECT_TRUE(names.count("folder/") > 0 || names.count("folder") > 0);
    EXPECT_TRUE(names.count("folder/sub/") > 0 || names.count("folder/sub") > 0);

    zip_close(archive);
}

TEST(ZipWriter, BuffersOutliveTheCallerScope)
{
    TempDir temp;
    const auto zip_path = temp.Get() / L"buffer.zip";

    {
        appbox::ZipWriter writer(zip_path.wstring());
        std::string error;
        {
            /* The temporary buffer is released before Close(). */
            const std::string payload("temporary buffer payload");
            ASSERT_TRUE(writer.AddFileBuffer("buffered.txt", payload.data(), payload.size(), error))
                << error;
        }
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    int error_code = 0;
    zip_t* archive = zip_open(appbox::WideToUTF8(zip_path.wstring()).c_str(), ZIP_RDONLY, &error_code);
    ASSERT_NE(archive, nullptr);
    EXPECT_EQ(ReadEntry(archive, "buffered.txt"), "temporary buffer payload");
    zip_close(archive);
}

TEST(ZipWriter, TruncatesAnExistingArchive)
{
    TempDir temp;
    const auto zip_path = temp.Get() / L"twice.zip";

    {
        appbox::ZipWriter writer(zip_path.wstring());
        std::string error;
        ASSERT_TRUE(writer.AddFileBuffer("first.txt", "1", 1, error)) << error;
        ASSERT_TRUE(writer.AddFileBuffer("second.txt", "2", 1, error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }
    {
        appbox::ZipWriter writer(zip_path.wstring());
        std::string error;
        ASSERT_TRUE(writer.AddFileBuffer("only.txt", "3", 1, error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    int error_code = 0;
    zip_t* archive = zip_open(appbox::WideToUTF8(zip_path.wstring()).c_str(), ZIP_RDONLY, &error_code);
    ASSERT_NE(archive, nullptr);

    const auto names = EntryNames(archive);
    EXPECT_EQ(names.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(ReadEntry(archive, "only.txt"), "3");

    zip_close(archive);
}

TEST(ZipWriter, KeepsEmptyDirectories)
{
    TempDir temp;
    const auto zip_path = temp.Get() / L"emptydir.zip";

    {
        appbox::ZipWriter writer(zip_path.wstring());
        std::string error;
        ASSERT_TRUE(writer.AddDirectory("filesystem/%ProgramFiles%/MyApp/empty", error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    int error_code = 0;
    zip_t* archive = zip_open(appbox::WideToUTF8(zip_path.wstring()).c_str(), ZIP_RDONLY, &error_code);
    ASSERT_NE(archive, nullptr);

    const auto names = EntryNames(archive);
    ASSERT_EQ(names.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(names.count("filesystem/%ProgramFiles%/MyApp/empty/") > 0
                || names.count("filesystem/%ProgramFiles%/MyApp/empty") > 0);

    zip_close(archive);
}
