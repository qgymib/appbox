#include <gtest/gtest.h>
#include "utils/Md5.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
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
        path_ = base / (L"appbox-md5-" + UniqueFragment());
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

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
 * @brief Write a file of the folder.
 * @param[in] path Path of the file.
 * @param[in] content Content of the file.
 * @return true on success.
 */
bool WriteFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return stream.good();
}

} // namespace

/**
 * Condition:
 * 1. An empty file exists.
 *
 * Expected:
 * 1. The digest is the digest of the empty content, which pins that the
 *    provider is fed with the content of the file and not with a block of
 *    uninitialized memory.
 */
TEST(Unit_Md5, EmptyFile)
{
    TempDir                     temp;
    const std::filesystem::path path = temp.Get() / L"empty.bin";
    ASSERT_TRUE(WriteFile(path, ""));

    std::string digest;
    std::string error;
    ASSERT_TRUE(appbox::Md5OfFile(path.wstring(), digest, error)) << error;
    EXPECT_EQ(digest, "d41d8cd98f00b204e9800998ecf8427e");
}

/**
 * Condition:
 * 1. A file holds the text `abc`.
 *
 * Expected:
 * 1. The digest is the well known digest of that text, so the digest of a
 *    file is the digest of its content.
 */
TEST(Unit_Md5, KnownText)
{
    TempDir                     temp;
    const std::filesystem::path path = temp.Get() / L"abc.txt";
    ASSERT_TRUE(WriteFile(path, "abc"));

    std::string digest;
    std::string error;
    ASSERT_TRUE(appbox::Md5OfFile(path.wstring(), digest, error)) << error;
    EXPECT_EQ(digest, "900150983cd24fb0d6963f7d28e17f72");
}

/**
 * Condition:
 * 1. A file holds one million `a` characters, which is far more than one
 *    block of the read buffer of the digest.
 *
 * Expected:
 * 1. The digest is the well known digest of that content, so every block of
 *    the file reaches the digest and the blocks are not truncated.
 */
TEST(Unit_Md5, LargeFileSpansMultipleBlocks)
{
    TempDir                     temp;
    const std::filesystem::path path = temp.Get() / L"large.bin";
    ASSERT_TRUE(WriteFile(path, std::string(1000000, 'a')));

    std::string digest;
    std::string error;
    ASSERT_TRUE(appbox::Md5OfFile(path.wstring(), digest, error)) << error;
    EXPECT_EQ(digest, "7707d6ae4e027c70eea2a935c2296f21");
}

/**
 * Condition:
 * 1. The file does not exist.
 *
 * Expected:
 * 1. The call fails, reports a reason and leaves the digest empty, so a
 *    caller cannot mistake a failure for the digest of an empty file.
 */
TEST(Unit_Md5, MissingFileIsReported)
{
    TempDir                     temp;
    const std::filesystem::path path = temp.Get() / L"missing.bin";

    std::string digest;
    std::string error;
    EXPECT_FALSE(appbox::Md5OfFile(path.wstring(), digest, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(digest.empty());
}
