#include <gtest/gtest.h>
#include "src/core/ProjectType.hpp"
#include <cstddef>
#include <set>
#include <string>

TEST(Unit_ProjectType, EveryTypeRoundTripsThroughItsToken)
{
    for (std::size_t index = 0; index < appbox::ProjectTypeCount(); ++index)
    {
        const auto type = appbox::ProjectTypeAt(index);

        appbox::ProjectType parsed = appbox::ProjectType::Standalone;
        ASSERT_TRUE(appbox::ParseProjectTypeToken(appbox::ProjectTypeToken(type), parsed));
        EXPECT_EQ(parsed, type);
    }
}

TEST(Unit_ProjectType, TokensAreTheSchemaSpelling)
{
    /* The tokens are the text form of the project file, so they are stable. */
    EXPECT_EQ(std::string(appbox::ProjectTypeToken(appbox::ProjectType::Standalone)), "standalone");
    EXPECT_EQ(std::string(appbox::ProjectTypeToken(appbox::ProjectType::Patch)), "patch");
}

TEST(Unit_ProjectType, ParseIgnoresTheCase)
{
    appbox::ProjectType parsed = appbox::ProjectType::Standalone;

    ASSERT_TRUE(appbox::ParseProjectTypeToken("PATCH", parsed));
    EXPECT_EQ(parsed, appbox::ProjectType::Patch);

    ASSERT_TRUE(appbox::ParseProjectTypeToken("Standalone", parsed));
    EXPECT_EQ(parsed, appbox::ProjectType::Standalone);
}

TEST(Unit_ProjectType, ParseRejectsAnUnknownToken)
{
    appbox::ProjectType parsed = appbox::ProjectType::Patch;

    EXPECT_FALSE(appbox::ParseProjectTypeToken("", parsed));
    EXPECT_FALSE(appbox::ParseProjectTypeToken("app", parsed));
    EXPECT_FALSE(appbox::ParseProjectTypeToken("stand-alone", parsed));
    EXPECT_FALSE(appbox::ParseProjectTypeToken("Standalone (ZIP)", parsed));

    /* A refused token leaves the caller untouched. */
    EXPECT_EQ(parsed, appbox::ProjectType::Patch);
}

TEST(Unit_ProjectType, DisplayNamesAreTheLabelsOfTheBox)
{
    const std::wstring standalone(appbox::ProjectTypeDisplayName(appbox::ProjectType::Standalone));
    const std::wstring patch(appbox::ProjectTypeDisplayName(appbox::ProjectType::Patch));

    EXPECT_EQ(standalone, L"Standalone (ZIP)");
    EXPECT_EQ(patch, L"Patch (ZIP)");
    EXPECT_NE(standalone, patch);

    /* Every entry of the box shows a label of its own. */
    std::set<std::wstring> labels;
    for (std::size_t index = 0; index < appbox::ProjectTypeCount(); ++index)
    {
        labels.insert(appbox::ProjectTypeDisplayName(appbox::ProjectTypeAt(index)));
    }
    EXPECT_EQ(labels.size(), appbox::ProjectTypeCount());
}

TEST(Unit_ProjectType, EveryEntryOfTheBoxIsIndexedAndFoundAgain)
{
    EXPECT_EQ(appbox::ProjectTypeCount(), static_cast<std::size_t>(2));

    for (std::size_t index = 0; index < appbox::ProjectTypeCount(); ++index)
    {
        EXPECT_EQ(appbox::ProjectTypeIndexOf(appbox::ProjectTypeAt(index)), index);
    }

    EXPECT_EQ(appbox::ProjectTypeIndexOf(appbox::ProjectType::Standalone), static_cast<std::size_t>(0));
    EXPECT_EQ(appbox::ProjectTypeIndexOf(appbox::ProjectType::Patch), static_cast<std::size_t>(1));
}

TEST(Unit_ProjectType, AnIndexOutsideTheBoxYieldsTheDefaultType)
{
    /*
     * An unknown selection never turns into an unexpected product: the box
     * falls back to the standalone archive.
     */
    EXPECT_EQ(appbox::ProjectTypeAt(appbox::ProjectTypeCount()), appbox::ProjectType::Standalone);
    EXPECT_EQ(appbox::ProjectTypeAt(static_cast<std::size_t>(-1)), appbox::ProjectType::Standalone);
}
