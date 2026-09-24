#include "filesystem/IsolationPolicy.hpp"
#include <gtest/gtest.h>

namespace
{

using appbox::FilesystemEntryKind;
using appbox::FilesystemIsolation;

} // namespace

/**
 * @brief `Full` of a folder hides the host subtree, `Full` of a file does not.
 */
TEST(UnitFilesystemIsolationPolicy, FullHidesTheHostForAFolderOnly)
{
    EXPECT_TRUE(appbox::filesystem::HidesHost(FilesystemIsolation::Full, FilesystemEntryKind::Directory));
    EXPECT_FALSE(appbox::filesystem::HidesHost(FilesystemIsolation::Full, FilesystemEntryKind::File));
}

/**
 * @brief `Write Copy` keeps the host entry visible for the whole subtree.
 */
TEST(UnitFilesystemIsolationPolicy, WriteCopyKeepsTheHostVisible)
{
    EXPECT_FALSE(appbox::filesystem::HidesHost(FilesystemIsolation::WriteCopy, FilesystemEntryKind::Directory));
    EXPECT_FALSE(appbox::filesystem::HidesHost(FilesystemIsolation::WriteCopy, FilesystemEntryKind::File));
}

/**
 * @brief `Whiteout` hides the host entry of a folder and of a file.
 */
TEST(UnitFilesystemIsolationPolicy, WhiteoutHidesTheHost)
{
    EXPECT_TRUE(appbox::filesystem::HidesHost(FilesystemIsolation::Whiteout, FilesystemEntryKind::Directory));
    EXPECT_TRUE(appbox::filesystem::HidesHost(FilesystemIsolation::Whiteout, FilesystemEntryKind::File));
}

/**
 * @brief Only `Whiteout` hides the content the packer imported.
 */
TEST(UnitFilesystemIsolationPolicy, OnlyWhiteoutHidesTheLowerLayers)
{
    EXPECT_FALSE(appbox::filesystem::HidesLower(FilesystemIsolation::Full));
    EXPECT_FALSE(appbox::filesystem::HidesLower(FilesystemIsolation::WriteCopy));
    EXPECT_TRUE(appbox::filesystem::HidesLower(FilesystemIsolation::Whiteout));
}

/**
 * @brief Only `Whiteout` makes an entry invisible until the sandbox holds it.
 */
TEST(UnitFilesystemIsolationPolicy, OnlyWhiteoutHidesTheEntry)
{
    EXPECT_FALSE(appbox::filesystem::HidesEntry(FilesystemIsolation::Full));
    EXPECT_FALSE(appbox::filesystem::HidesEntry(FilesystemIsolation::WriteCopy));
    EXPECT_TRUE(appbox::filesystem::HidesEntry(FilesystemIsolation::Whiteout));
}

/**
 * @brief The decision table of the policy in one place.
 *
 * The rows are the table of `filesystem/IsolationPolicy.hpp`: the mode and the
 * kind of the closest listed entry decide which layers stay visible.
 */
TEST(UnitFilesystemIsolationPolicy, DecisionTable)
{
    struct Row
    {
        FilesystemIsolation   mode;
        FilesystemEntryKind   kind;
        bool                  hides_host;
        bool                  hides_lower;
        bool                  hides_entry;
    };

    const Row rows[] = {
        { FilesystemIsolation::Full,      FilesystemEntryKind::Directory, true,  false, false },
        { FilesystemIsolation::Full,      FilesystemEntryKind::File,      false, false, false },
        { FilesystemIsolation::WriteCopy, FilesystemEntryKind::Directory, false, false, false },
        { FilesystemIsolation::WriteCopy, FilesystemEntryKind::File,      false, false, false },
        { FilesystemIsolation::Whiteout,  FilesystemEntryKind::Directory, true,  true,  true  },
        { FilesystemIsolation::Whiteout,  FilesystemEntryKind::File,      true,  true,  true  },
    };

    for (const auto& row : rows)
    {
        EXPECT_EQ(appbox::filesystem::HidesHost(row.mode, row.kind), row.hides_host);
        EXPECT_EQ(appbox::filesystem::HidesLower(row.mode), row.hides_lower);
        EXPECT_EQ(appbox::filesystem::HidesEntry(row.mode), row.hides_entry);
    }
}
