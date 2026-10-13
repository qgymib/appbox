#include <gtest/gtest.h>
#include "src/core/CompressionLevel.hpp"
#include <cstddef>
#include <set>
#include <string>

TEST(Unit_CompressionLevel, EveryLevelRoundTripsThroughItsToken)
{
    for (std::size_t index = 0; index < appbox::CompressionLevelCount(); ++index)
    {
        const auto level = appbox::CompressionLevelAt(index);

        appbox::CompressionLevel parsed = appbox::CompressionLevel::StoreOnly;
        ASSERT_TRUE(appbox::ParseCompressionLevelToken(appbox::CompressionLevelToken(level), parsed));
        EXPECT_EQ(parsed, level);
    }
}

TEST(Unit_CompressionLevel, TokensAreTheSchemaSpelling)
{
    /* The tokens are the text form of the project file, so they are stable. */
    EXPECT_EQ(std::string(appbox::CompressionLevelToken(appbox::CompressionLevel::StoreOnly)), "store_only");
    EXPECT_EQ(std::string(appbox::CompressionLevelToken(appbox::CompressionLevel::Fastest)), "fastest");
    EXPECT_EQ(std::string(appbox::CompressionLevelToken(appbox::CompressionLevel::Fast)), "fast");
    EXPECT_EQ(std::string(appbox::CompressionLevelToken(appbox::CompressionLevel::Normal)), "normal");
    EXPECT_EQ(std::string(appbox::CompressionLevelToken(appbox::CompressionLevel::Maximum)), "maximum");
    EXPECT_EQ(std::string(appbox::CompressionLevelToken(appbox::CompressionLevel::Extreme)), "extreme");
}

TEST(Unit_CompressionLevel, ParseIgnoresTheCase)
{
    appbox::CompressionLevel parsed = appbox::CompressionLevel::StoreOnly;

    ASSERT_TRUE(appbox::ParseCompressionLevelToken("EXTREME", parsed));
    EXPECT_EQ(parsed, appbox::CompressionLevel::Extreme);

    ASSERT_TRUE(appbox::ParseCompressionLevelToken("Store_Only", parsed));
    EXPECT_EQ(parsed, appbox::CompressionLevel::StoreOnly);
}

TEST(Unit_CompressionLevel, ParseRejectsAnUnknownToken)
{
    appbox::CompressionLevel parsed = appbox::CompressionLevel::Maximum;

    EXPECT_FALSE(appbox::ParseCompressionLevelToken("", parsed));
    EXPECT_FALSE(appbox::ParseCompressionLevelToken("5", parsed));
    EXPECT_FALSE(appbox::ParseCompressionLevelToken("best", parsed));
    EXPECT_FALSE(appbox::ParseCompressionLevelToken("store only", parsed));
    EXPECT_FALSE(appbox::ParseCompressionLevelToken("5: Normal", parsed));

    /* A refused token leaves the caller untouched. */
    EXPECT_EQ(parsed, appbox::CompressionLevel::Maximum);
}

TEST(Unit_CompressionLevel, DisplayNamesAreTheLabelsOfTheBox)
{
    EXPECT_EQ(std::wstring(appbox::CompressionLevelDisplayName(appbox::CompressionLevel::StoreOnly)), L"0: Store Only");
    EXPECT_EQ(std::wstring(appbox::CompressionLevelDisplayName(appbox::CompressionLevel::Fastest)), L"1: Fastest");
    EXPECT_EQ(std::wstring(appbox::CompressionLevelDisplayName(appbox::CompressionLevel::Fast)), L"3: Fast");
    EXPECT_EQ(std::wstring(appbox::CompressionLevelDisplayName(appbox::CompressionLevel::Normal)), L"5: Normal");
    EXPECT_EQ(std::wstring(appbox::CompressionLevelDisplayName(appbox::CompressionLevel::Maximum)), L"7: Maximum");
    EXPECT_EQ(std::wstring(appbox::CompressionLevelDisplayName(appbox::CompressionLevel::Extreme)), L"9: Extreme");

    /* Every entry of the box shows a label of its own. */
    std::set<std::wstring> labels;
    for (std::size_t index = 0; index < appbox::CompressionLevelCount(); ++index)
    {
        labels.insert(appbox::CompressionLevelDisplayName(appbox::CompressionLevelAt(index)));
    }
    EXPECT_EQ(labels.size(), appbox::CompressionLevelCount());
}

TEST(Unit_CompressionLevel, ValuesAreTheLevelsOfTheScale)
{
    /*
     * The value of a level is the zlib level the packer asks libzip for, and
     * the level which stores the entries asks for no compression at all.
     */
    EXPECT_EQ(appbox::CompressionLevelValue(appbox::CompressionLevel::StoreOnly), 0);
    EXPECT_EQ(appbox::CompressionLevelValue(appbox::CompressionLevel::Fastest), 1);
    EXPECT_EQ(appbox::CompressionLevelValue(appbox::CompressionLevel::Fast), 3);
    EXPECT_EQ(appbox::CompressionLevelValue(appbox::CompressionLevel::Normal), 5);
    EXPECT_EQ(appbox::CompressionLevelValue(appbox::CompressionLevel::Maximum), 7);
    EXPECT_EQ(appbox::CompressionLevelValue(appbox::CompressionLevel::Extreme), 9);
}

TEST(Unit_CompressionLevel, TheBoxListsTheScaleInAscendingOrder)
{
    EXPECT_EQ(appbox::CompressionLevelCount(), static_cast<std::size_t>(6));

    for (std::size_t index = 0; index < appbox::CompressionLevelCount(); ++index)
    {
        EXPECT_EQ(appbox::CompressionLevelIndexOf(appbox::CompressionLevelAt(index)), index);

        if (index > 0)
        {
            const auto previous = appbox::CompressionLevelValue(appbox::CompressionLevelAt(index - 1));
            const auto current = appbox::CompressionLevelValue(appbox::CompressionLevelAt(index));
            EXPECT_LT(previous, current);
        }
    }
}

TEST(Unit_CompressionLevel, TheDefaultLevelIsTheNormalOne)
{
    EXPECT_EQ(appbox::kDefaultCompressionLevel, appbox::CompressionLevel::Normal);
    EXPECT_EQ(appbox::CompressionLevelValue(appbox::kDefaultCompressionLevel), 5);

    /* The default level is part of the box, so it can be shown by the selection. */
    EXPECT_LT(appbox::CompressionLevelIndexOf(appbox::kDefaultCompressionLevel), appbox::CompressionLevelCount());
}

TEST(Unit_CompressionLevel, AnIndexOutsideTheBoxYieldsTheDefaultLevel)
{
    /*
     * An unknown selection never turns into an unexpected product: the box
     * falls back to the level which compresses the entries.
     */
    EXPECT_EQ(appbox::CompressionLevelAt(appbox::CompressionLevelCount()), appbox::CompressionLevel::Normal);
    EXPECT_EQ(appbox::CompressionLevelAt(static_cast<std::size_t>(-1)), appbox::CompressionLevel::Normal);
}

TEST(Unit_CompressionLevel, ALevelOutsideTheBoxYieldsTheDefaultSpelling)
{
    const auto unknown = static_cast<appbox::CompressionLevel>(99);

    EXPECT_EQ(appbox::CompressionLevelToken(unknown), appbox::CompressionLevelToken(appbox::kDefaultCompressionLevel));
    EXPECT_EQ(appbox::CompressionLevelDisplayName(unknown),
              appbox::CompressionLevelDisplayName(appbox::kDefaultCompressionLevel));
    EXPECT_EQ(appbox::CompressionLevelValue(unknown), appbox::CompressionLevelValue(appbox::kDefaultCompressionLevel));
    EXPECT_EQ(appbox::CompressionLevelIndexOf(unknown),
              appbox::CompressionLevelIndexOf(appbox::kDefaultCompressionLevel));
}
