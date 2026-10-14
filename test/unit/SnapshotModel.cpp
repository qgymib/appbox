#include <gtest/gtest.h>
#include "src/core/SnapshotModel.hpp"
#include <cstdint>
#include <string>

TEST(Unit_SnapshotModel, FileLookupKeyIgnoresTheCaseOfThePath)
{
    /* The host filesystem compares its names without regard to the case. */
    EXPECT_EQ(appbox::snapshot::FileLookupKey("program_files", L"Foo\\Bar.exe"),
              appbox::snapshot::FileLookupKey("program_files", L"foo\\bar.EXE"));
}

TEST(Unit_SnapshotModel, FileLookupKeySeparatesThePresetDirectories)
{
    EXPECT_NE(appbox::snapshot::FileLookupKey("program_files", L"Foo"),
              appbox::snapshot::FileLookupKey("user_profile", L"Foo"));
}

TEST(Unit_SnapshotModel, FileLookupKeyKeepsThePresetIdentifierAsItIs)
{
    /*
     * The identifier is a name of the model and not a name of the host, so it
     * is part of the key as the preset table spells it.
     */
    EXPECT_NE(appbox::snapshot::FileLookupKey("program_files", L"a"),
              appbox::snapshot::FileLookupKey("PROGRAM_FILES", L"a"));
}

TEST(Unit_SnapshotModel, FileLookupKeyDoesNotMergeAPathAndItsChild)
{
    EXPECT_NE(appbox::snapshot::FileLookupKey("program_files", L"Foo"),
              appbox::snapshot::FileLookupKey("program_files", L"Foo\\Bar"));
}

TEST(Unit_SnapshotModel, RegistryValueLookupKeyIgnoresTheCaseOfBothNames)
{
    EXPECT_EQ(appbox::snapshot::RegistryValueLookupKey(L"HKEY_CURRENT_USER\\Software\\Foo", L"Server"),
              appbox::snapshot::RegistryValueLookupKey(L"hkey_current_user\\software\\foo", L"server"));
}

TEST(Unit_SnapshotModel, RegistryValueLookupKeySeparatesKeysAndValues)
{
    EXPECT_NE(appbox::snapshot::RegistryValueLookupKey(L"HKEY_CURRENT_USER\\Foo", L"Bar"),
              appbox::snapshot::RegistryValueLookupKey(L"HKEY_CURRENT_USER", L"Foo\\Bar"));
}

TEST(Unit_SnapshotModel, RegistryValueLookupKeySeparatesTheDefaultValue)
{
    /* The default value of a key is a value of its own, named by an empty text. */
    EXPECT_NE(appbox::snapshot::RegistryValueLookupKey(L"HKEY_CURRENT_USER\\Foo", L""),
              appbox::snapshot::RegistryValueLookupKey(L"HKEY_CURRENT_USER\\Foo", L"Bar"));
}

TEST(Unit_SnapshotModel, RegistryKeyLookupKeyIgnoresTheCase)
{
    EXPECT_EQ(appbox::snapshot::RegistryKeyLookupKey(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Foo"),
              appbox::snapshot::RegistryKeyLookupKey(L"hkey_local_machine\\software\\foo"));
}

TEST(Unit_SnapshotModel, EnvironmentLookupKeyIgnoresTheCase)
{
    EXPECT_EQ(appbox::snapshot::EnvironmentLookupKey(L"Path"), appbox::snapshot::EnvironmentLookupKey(L"PATH"));
    EXPECT_NE(appbox::snapshot::EnvironmentLookupKey(L"PATH"), appbox::snapshot::EnvironmentLookupKey(L"PATHEXT"));
}

TEST(Unit_SnapshotModel, FoldTextKeepsTheLengthOfTheText)
{
    const auto folded = appbox::snapshot::FoldText(L"AbC\\xY");
    EXPECT_EQ(folded, L"ABC\\XY");
    EXPECT_EQ(folded.size(), static_cast<std::size_t>(6));
}
