#include "utils/WinCall.hpp" /* Must be first include file */
#include <gtest/gtest.h>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
#include "utils/MapBaseFS.hpp"

namespace
{

/**
 * @brief Build a unique temporary directory path.
 * @return The path of a directory which does not exist yet.
 */
std::filesystem::path MakeTempDirPath()
{
    std::random_device rd;
    std::mt19937_64    gen(rd());

    return std::filesystem::temp_directory_path() / ("appbox_unit_" + std::to_string(gen()));
}

/**
 * @brief Fixture which resolves the NT path conversion helpers.
 */
class UnitLoaderPath : public testing::Test
{
protected:
    void SetUp() override
    {
        appbox::WinCallInit();
    }
};

} // namespace

/**
 * @brief An empty base filesystem path is rejected instead of reading past the
 *        end of the path string.
 */
TEST_F(UnitLoaderPath, EmptyPathIsRejected)
{
    std::vector<appbox::SandboxLowerFS> mapped;

    EXPECT_NE(appbox::MapBaseFS("", mapped), 0u);
    EXPECT_TRUE(mapped.empty());
}

/**
 * @brief A path which only consists of separators is rejected as well.
 */
TEST_F(UnitLoaderPath, SeparatorsOnlyPathIsRejected)
{
    std::vector<appbox::SandboxLowerFS> mapped;

    EXPECT_NE(appbox::MapBaseFS("\\\\\\\\", mapped), 0u);
    EXPECT_TRUE(mapped.empty());
}

/**
 * @brief A base filesystem which does not exist is reported instead of being
 *        treated as an empty layer set.
 */
TEST_F(UnitLoaderPath, MissingDirectoryIsRejected)
{
    std::vector<appbox::SandboxLowerFS> mapped;

    EXPECT_NE(appbox::MapBaseFS(MakeTempDirPath().string(), mapped), 0u);
    EXPECT_TRUE(mapped.empty());
}

/**
 * @brief A known folder layer is mapped to an NT path.
 */
TEST_F(UnitLoaderPath, KnownFolderLayerIsMapped)
{
    const std::filesystem::path root = MakeTempDirPath();
    std::filesystem::create_directories(root / "filesystem" / "%APPDATA%");

    std::vector<appbox::SandboxLowerFS> mapped;
    ASSERT_EQ(appbox::MapBaseFS(root.string(), mapped), 0u);
    ASSERT_EQ(mapped.size(), 1u);

    EXPECT_FALSE(mapped[0].host_nt_path.empty());
    EXPECT_FALSE(mapped[0].mapped_nt_path.empty());

    std::filesystem::remove_all(root);
}

/**
 * @brief A drive letter layer is mapped to the drive itself.
 */
TEST_F(UnitLoaderPath, DriveLetterLayerIsMapped)
{
    const std::filesystem::path root = MakeTempDirPath();
    std::filesystem::create_directories(root / "filesystem" / "C");

    std::vector<appbox::SandboxLowerFS> mapped;
    ASSERT_EQ(appbox::MapBaseFS(root.string(), mapped), 0u);
    ASSERT_EQ(mapped.size(), 1u);

    EXPECT_FALSE(mapped[0].host_nt_path.empty());
    EXPECT_FALSE(mapped[0].mapped_nt_path.empty());

    std::filesystem::remove_all(root);
}

/**
 * @brief The reserved layers of the other isolation domains are skipped.
 */
TEST_F(UnitLoaderPath, ReservedLayersAreSkipped)
{
    const std::filesystem::path root = MakeTempDirPath();
    std::filesystem::create_directories(root / "filesystem" / "%REGISTRY%");

    std::vector<appbox::SandboxLowerFS> mapped;
    ASSERT_EQ(appbox::MapBaseFS(root.string(), mapped), 0u);
    EXPECT_TRUE(mapped.empty());

    std::filesystem::remove_all(root);
}

/**
 * @brief A layer which is neither a known folder nor a drive letter is an error.
 */
TEST_F(UnitLoaderPath, UnknownLayerIsRejected)
{
    const std::filesystem::path root = MakeTempDirPath();
    std::filesystem::create_directories(root / "filesystem" / "not_a_known_folder");

    std::vector<appbox::SandboxLowerFS> mapped;
    EXPECT_NE(appbox::MapBaseFS(root.string(), mapped), 0u);

    std::filesystem::remove_all(root);
}

/**
 * @brief A trailing separator is removed before the layer directory is looked
 *        up, the path is still accepted.
 */
TEST_F(UnitLoaderPath, TrailingSeparatorIsAccepted)
{
    const std::filesystem::path root = MakeTempDirPath();
    std::filesystem::create_directories(root / "filesystem" / "%APPDATA%");

    std::vector<appbox::SandboxLowerFS> mapped;
    ASSERT_EQ(appbox::MapBaseFS(root.string() + "\\", mapped), 0u);
    EXPECT_EQ(mapped.size(), 1u);

    std::filesystem::remove_all(root);
}
