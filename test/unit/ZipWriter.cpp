#include <gtest/gtest.h>
#include "src/core/ZipWriter.hpp"
#include "WString.hpp"
#include <zip.h>
#include <chrono>
#include <cstdint>
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
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
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
    const auto            count = zip_get_num_entries(archive, 0);
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

/**
 * @brief Read the stat of one zip entry.
 * @param[in] archive Open zip archive.
 * @param[in] name Entry name.
 * @param[out] stat Stat of the entry.
 * @return true when the entry exists and its stat could be read.
 */
bool EntryStat(zip_t* archive, const char* name, zip_stat_t& stat)
{
    const auto index = zip_name_locate(archive, name, 0);
    if (index < 0)
    {
        return false;
    }

    zip_stat_init(&stat);
    return zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &stat) == 0;
}

/**
 * @brief Generate content deflate shrinks a lot.
 * @param[in] size Size of the content in bytes.
 * @return The content.
 */
std::string CompressibleContent(std::size_t size)
{
    std::string content;
    content.reserve(size);
    for (std::size_t index = 0; index < size; ++index)
    {
        content.push_back(static_cast<char>('A' + (index % 8)));
    }
    return content;
}

/**
 * @brief Generate content deflate cannot shrink.
 *
 * The bytes come from a xorshift sequence, so the content carries no pattern a
 * compressor could exploit.
 *
 * @param[in] size Size of the content in bytes.
 * @return The content.
 */
std::string IncompressibleContent(std::size_t size)
{
    std::string   content;
    std::uint32_t state = 0x12345678u;
    content.reserve(size);
    for (std::size_t index = 0; index < size; ++index)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        content.push_back(static_cast<char>(state & 0xFFu));
    }
    return content;
}

/**
 * @brief Open an archive written by a test.
 * @param[in] zip_path Path of the archive.
 * @return The open archive; the caller closes it.
 */
zip_t* OpenArchive(const std::filesystem::path& zip_path)
{
    int    error_code = 0;
    zip_t* archive = zip_open(appbox::WideToUTF8(zip_path.wstring()).c_str(), ZIP_RDONLY, &error_code);
    return archive;
}

} // namespace

TEST(Unit_ZipWriter, WritesEntriesReadableByLibzip)
{
    TempDir    temp;
    const auto zip_path = temp.Get() / L"archive.zip";

    std::filesystem::path disk_file;
    {
        const auto disk_path = temp.Get() / L"payload.bin";
        FILE*      handle = nullptr;
        ASSERT_EQ(_wfopen_s(&handle, disk_path.wstring().c_str(), L"wb"), 0);
        ASSERT_NE(handle, nullptr);
        fwrite("DISK", 1, 4, handle);
        fclose(handle);
        disk_file = disk_path;

        appbox::ZipWriter writer(zip_path.wstring());
        std::string       error;
        ASSERT_TRUE(writer.AddDirectory("folder", error)) << error;
        ASSERT_TRUE(writer.AddDirectory("folder/sub", error)) << error;
        ASSERT_TRUE(writer.AddFileBuffer("folder/sub/file.txt", "hello", 5, error)) << error;
        ASSERT_TRUE(writer.AddFileDisk(disk_path.wstring(), "root.bin", error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    int    error_code = 0;
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

TEST(Unit_ZipWriter, BuffersOutliveTheCallerScope)
{
    TempDir    temp;
    const auto zip_path = temp.Get() / L"buffer.zip";

    {
        appbox::ZipWriter writer(zip_path.wstring());
        std::string       error;
        {
            /* The temporary buffer is released before Close(). */
            const std::string payload("temporary buffer payload");
            ASSERT_TRUE(writer.AddFileBuffer("buffered.txt", payload.data(), payload.size(), error)) << error;
        }
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    int    error_code = 0;
    zip_t* archive = zip_open(appbox::WideToUTF8(zip_path.wstring()).c_str(), ZIP_RDONLY, &error_code);
    ASSERT_NE(archive, nullptr);
    EXPECT_EQ(ReadEntry(archive, "buffered.txt"), "temporary buffer payload");
    zip_close(archive);
}

TEST(Unit_ZipWriter, TruncatesAnExistingArchive)
{
    TempDir    temp;
    const auto zip_path = temp.Get() / L"twice.zip";

    {
        appbox::ZipWriter writer(zip_path.wstring());
        std::string       error;
        ASSERT_TRUE(writer.AddFileBuffer("first.txt", "1", 1, error)) << error;
        ASSERT_TRUE(writer.AddFileBuffer("second.txt", "2", 1, error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }
    {
        appbox::ZipWriter writer(zip_path.wstring());
        std::string       error;
        ASSERT_TRUE(writer.AddFileBuffer("only.txt", "3", 1, error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    int    error_code = 0;
    zip_t* archive = zip_open(appbox::WideToUTF8(zip_path.wstring()).c_str(), ZIP_RDONLY, &error_code);
    ASSERT_NE(archive, nullptr);

    const auto names = EntryNames(archive);
    EXPECT_EQ(names.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(ReadEntry(archive, "only.txt"), "3");

    zip_close(archive);
}

TEST(Unit_ZipWriter, KeepsEmptyDirectories)
{
    TempDir    temp;
    const auto zip_path = temp.Get() / L"emptydir.zip";

    {
        appbox::ZipWriter writer(zip_path.wstring());
        std::string       error;
        ASSERT_TRUE(writer.AddDirectory("filesystem/#ProgramFiles#/MyApp/empty", error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    int    error_code = 0;
    zip_t* archive = zip_open(appbox::WideToUTF8(zip_path.wstring()).c_str(), ZIP_RDONLY, &error_code);
    ASSERT_NE(archive, nullptr);

    const auto names = EntryNames(archive);
    ASSERT_EQ(names.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(names.count("filesystem/#ProgramFiles#/MyApp/empty/") > 0 ||
                names.count("filesystem/#ProgramFiles#/MyApp/empty") > 0);

    zip_close(archive);
}

TEST(Unit_ZipWriter, StoreOnlyWritesTheContentWithoutCompressingIt)
{
    TempDir    temp;
    const auto zip_path = temp.Get() / L"stored.zip";
    const auto payload = CompressibleContent(64 * 1024);

    {
        appbox::ZipWriter writer(zip_path.wstring(), appbox::CompressionLevel::StoreOnly);
        std::string       error;
        ASSERT_TRUE(writer.AddFileBuffer("content.bin", payload.data(), payload.size(), error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    zip_t* archive = OpenArchive(zip_path);
    ASSERT_NE(archive, nullptr);

    zip_stat_t stat = {};
    ASSERT_TRUE(EntryStat(archive, "content.bin", stat));

    /* The entry is written as it is, so it neither shrinks nor grows. */
    EXPECT_EQ(stat.comp_method, ZIP_CM_STORE);
    EXPECT_EQ(stat.size, static_cast<zip_uint64_t>(payload.size()));
    EXPECT_EQ(stat.comp_size, static_cast<zip_uint64_t>(payload.size()));
    EXPECT_EQ(ReadEntry(archive, "content.bin"), payload);

    zip_close(archive);
}

TEST(Unit_ZipWriter, TheDefaultLevelCompressesTheEntries)
{
    TempDir    temp;
    const auto zip_path = temp.Get() / L"default.zip";
    const auto payload = CompressibleContent(64 * 1024);

    {
        /* A writer which names no level compresses with the default level. */
        appbox::ZipWriter writer(zip_path.wstring());
        std::string       error;
        ASSERT_TRUE(writer.AddFileBuffer("content.bin", payload.data(), payload.size(), error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    zip_t* archive = OpenArchive(zip_path);
    ASSERT_NE(archive, nullptr);

    zip_stat_t stat = {};
    ASSERT_TRUE(EntryStat(archive, "content.bin", stat));

    EXPECT_EQ(stat.comp_method, ZIP_CM_DEFLATE);
    EXPECT_LT(stat.comp_size, static_cast<zip_uint64_t>(payload.size()));
    EXPECT_EQ(ReadEntry(archive, "content.bin"), payload);

    zip_close(archive);
}

TEST(Unit_ZipWriter, ACompressingLevelDeflatesTheEntries)
{
    TempDir    temp;
    const auto zip_path = temp.Get() / L"extreme.zip";
    const auto payload = CompressibleContent(64 * 1024);

    {
        appbox::ZipWriter writer(zip_path.wstring(), appbox::CompressionLevel::Extreme);
        std::string       error;
        ASSERT_TRUE(writer.AddFileBuffer("content.bin", payload.data(), payload.size(), error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    zip_t* archive = OpenArchive(zip_path);
    ASSERT_NE(archive, nullptr);

    zip_stat_t stat = {};
    ASSERT_TRUE(EntryStat(archive, "content.bin", stat));

    EXPECT_EQ(stat.comp_method, ZIP_CM_DEFLATE);
    EXPECT_LT(stat.comp_size, static_cast<zip_uint64_t>(payload.size()));
    EXPECT_EQ(ReadEntry(archive, "content.bin"), payload);

    zip_close(archive);
}

TEST(Unit_ZipWriter, ACompressingLevelKeepsAnEntryWhichDoesNotShrink)
{
    TempDir    temp;
    const auto zip_path = temp.Get() / L"incompressible.zip";
    const auto payload = IncompressibleContent(512);

    {
        appbox::ZipWriter writer(zip_path.wstring(), appbox::CompressionLevel::Extreme);
        std::string       error;
        ASSERT_TRUE(writer.AddFileBuffer("content.bin", payload.data(), payload.size(), error)) << error;
        ASSERT_TRUE(writer.Close(error)) << error;
    }

    zip_t* archive = OpenArchive(zip_path);
    ASSERT_NE(archive, nullptr);

    zip_stat_t stat = {};
    ASSERT_TRUE(EntryStat(archive, "content.bin", stat));

    /*
     * A compressing level never makes an entry larger than a stored one: libzip
     * holds the whole content of a single read while it deflates an entry and
     * writes the stored data when deflating it did not pay off.
     */
    EXPECT_EQ(stat.comp_method, ZIP_CM_STORE);
    EXPECT_EQ(stat.comp_size, static_cast<zip_uint64_t>(payload.size()));
    EXPECT_EQ(ReadEntry(archive, "content.bin"), payload);

    zip_close(archive);
}
