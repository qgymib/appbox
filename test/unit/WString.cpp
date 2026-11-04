#include <gtest/gtest.h>
#include <string>
#include "utils/ErrorCodeText.hpp"
#include "WString.hpp"

/**
 * @brief A null narrow string cannot be converted, the conversion helper has to
 *        report the failure by its code instead of allocating a zero sized
 *        buffer and reading past it.
 */
TEST(Unit_WString, Utf8ToWideNullInputIsReportedByItsCode)
{
    const auto converted = appbox::UTF8ToWide(static_cast<const char*>(nullptr));

    ASSERT_FALSE(converted) << "a null pointer names no text";
    EXPECT_EQ(converted.error(), appbox::ErrorCode::kTextInputNull)
        << appbox::test::DescribeErrorCode(converted.error());

    /* A refused conversion carries no value at all, which is what tells the
     * error side of the answer from an empty text. */
    EXPECT_FALSE(converted.has_value());
}

/**
 * @brief A null wide string cannot be converted, the conversion helper reports
 *        the failure by its code instead of throwing.
 */
TEST(Unit_WString, WideToUTF8NullInputIsReportedByItsCode)
{
    const auto converted = appbox::WideToUTF8(static_cast<const wchar_t*>(nullptr));

    ASSERT_FALSE(converted) << "a null pointer names no text";
    EXPECT_EQ(converted.error(), appbox::ErrorCode::kTextInputNull)
        << appbox::test::DescribeErrorCode(converted.error());

    /* A refused conversion carries no value at all, which is what tells the
     * error side of the answer from an empty text. */
    EXPECT_FALSE(converted.has_value());
}

/**
 * @brief A byte sequence which is not UTF-8 is refused instead of being
 *        replaced by a placeholder.
 *
 * The three bytes are the encoding of the unpaired high surrogate `U+D800`,
 * which has no wide form: a helper which repaired the text would hand back a
 * name the caller never spelled, so the conversion reports the class of the
 * failure and carries no value.
 */
TEST(Unit_WString, Utf8ToWideReportsATextTheCodePageCannotCarry)
{
    const auto converted = appbox::UTF8ToWide(std::string("\xED\xA0\x80"));

    ASSERT_FALSE(converted) << "an unpaired surrogate names no wide text";
    EXPECT_EQ(converted.error(), appbox::ErrorCode::kTextConversionFailed)
        << appbox::test::DescribeErrorCode(converted.error());
    EXPECT_FALSE(converted.has_value());
}

/**
 * @brief Round trip a plain ASCII string, the behaviour of the conversion
 *        helpers for valid input must not change.
 */
TEST(Unit_WString, RoundTripAscii)
{
    const std::string utf8 = "appbox-\\??\\C:\\Users\\foo";

    const auto wide = appbox::UTF8ToWide(utf8);
    ASSERT_TRUE(wide) << appbox::test::DescribeErrorCode(wide.error());

    const auto narrow = appbox::WideToUTF8(*wide);
    ASSERT_TRUE(narrow) << appbox::test::DescribeErrorCode(narrow.error());
    EXPECT_EQ(*narrow, utf8);
}

/**
 * @brief Round trip a non ASCII string, the helpers are documented to use the
 *        UTF-8 code page.
 */
TEST(Unit_WString, RoundTripNonAscii)
{
    const std::string utf8 = "\xE6\xB2\x99\xE7\xAE\xB1"; /* "沙箱" in UTF-8 */

    const auto wide = appbox::UTF8ToWide(utf8);
    ASSERT_TRUE(wide) << appbox::test::DescribeErrorCode(wide.error());

    const auto narrow = appbox::WideToUTF8(*wide);
    ASSERT_TRUE(narrow) << appbox::test::DescribeErrorCode(narrow.error());
    EXPECT_EQ(*narrow, utf8);
}

/**
 * @brief An empty narrow text converts to an empty wide text and not to a
 *        refusal, so a caller can tell "no text" from "no conversion".
 */
TEST(Unit_WString, EmptyNarrowTextIsConvertedToEmptyText)
{
    const auto converted = appbox::UTF8ToWide(std::string());

    ASSERT_TRUE(converted) << appbox::test::DescribeErrorCode(converted.error());
    EXPECT_TRUE(converted->empty());
}

/**
 * @brief An empty wide text converts to an empty narrow text and not to a
 *        refusal, so a caller can tell "no text" from "no conversion".
 */
TEST(Unit_WString, EmptyWideTextIsConvertedToEmptyText)
{
    const auto converted = appbox::WideToUTF8(std::wstring());

    ASSERT_TRUE(converted) << appbox::test::DescribeErrorCode(converted.error());
    EXPECT_TRUE(converted->empty());
}

/**
 * @brief A long text is converted whole, the two step conversion of the helper
 *        is not limited by a fixed buffer.
 */
TEST(Unit_WString, LongTextIsConvertedWhole)
{
    const std::string utf8(4096, 'x');

    const auto wide = appbox::UTF8ToWide(utf8);
    ASSERT_TRUE(wide) << appbox::test::DescribeErrorCode(wide.error());

    const auto narrow = appbox::WideToUTF8(*wide);
    ASSERT_TRUE(narrow) << appbox::test::DescribeErrorCode(narrow.error());
    EXPECT_EQ(*narrow, utf8);
}

/**
 * @brief A prefix a text starts with is replaced by the replacement text, and
 *        the rest of the text is kept as it is.
 */
TEST(Unit_WString, PrefixCompareExchangeReplacesAMatchingPrefix)
{
    const auto replaced = appbox::PrefixCompareExchange(L"\\??\\C:\\Users\\foo", L"\\??\\C:", L"\\A\\view", true);

    ASSERT_TRUE(replaced) << appbox::test::DescribeErrorCode(replaced.error());
    EXPECT_EQ(*replaced, L"\\A\\view\\Users\\foo");
}

/**
 * @brief The comparison folds the case in the way the caller asks for it, which
 *        is what makes the paths of one layer match whatever case the
 *        application spells them with.
 */
TEST(Unit_WString, PrefixCompareExchangeHonoursTheCase)
{
    const auto folded = appbox::PrefixCompareExchange(L"c:\\users", L"C:", L"D:", true);
    ASSERT_TRUE(folded) << appbox::test::DescribeErrorCode(folded.error());
    EXPECT_EQ(*folded, L"D:\\users");

    const auto exact = appbox::PrefixCompareExchange(L"c:\\users", L"C:", L"D:", false);
    ASSERT_FALSE(exact) << "the two prefixes differ in one character";
    EXPECT_EQ(exact.error(), appbox::ErrorCode::kPrefixMismatch) << appbox::test::DescribeErrorCode(exact.error());
}

/**
 * @brief An empty prefix matches every text, which is the answer the call gives
 *        for a layer whose mapped path is empty.
 */
TEST(Unit_WString, PrefixCompareExchangeOfAnEmptyPrefixMatches)
{
    const auto replaced = appbox::PrefixCompareExchange(L"C:\\Users", L"", L"X", true);

    ASSERT_TRUE(replaced) << appbox::test::DescribeErrorCode(replaced.error());
    EXPECT_EQ(*replaced, L"XC:\\Users");
}

/**
 * @brief A text which does not start with the prefix is reported by its code
 *        and carries no value, so a caller which uses the comparison as an
 *        ordinary branch reads the error side.
 */
TEST(Unit_WString, PrefixCompareExchangeReportsAMismatch)
{
    const auto replaced = appbox::PrefixCompareExchange(L"C:\\Users", L"D:", L"X", true);

    ASSERT_FALSE(replaced) << "the text does not start with the prefix";
    EXPECT_EQ(replaced.error(), appbox::ErrorCode::kPrefixMismatch)
        << appbox::test::DescribeErrorCode(replaced.error());
    EXPECT_FALSE(replaced.has_value());

    /* A prefix which is longer than the text is a mismatch as well. */
    const auto shorter = appbox::PrefixCompareExchange(L"C:", L"C:\\Users", L"X", true);
    ASSERT_FALSE(shorter);
    EXPECT_EQ(shorter.error(), appbox::ErrorCode::kPrefixMismatch) << appbox::test::DescribeErrorCode(shorter.error());
}

/**
 * @brief The split helper answers the text itself when the delimiter is empty,
 *        which is the answer the extraction relies on for a name without a
 *        separator.
 */
TEST(Unit_WString, SplitWithoutADelimiterAnswersTheTextItself)
{
    const auto parts = appbox::Split(L"C:\\Users\\foo", L"");

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts.front(), L"C:\\Users\\foo");
}

/**
 * @brief The split helper drops the delimiter and keeps the empty segments it
 *        separates, which is what the callers expect of a path.
 */
TEST(Unit_WString, SplitKeepsTheEmptySegments)
{
    const auto parts = appbox::Split(L"a\\\\b\\", L"\\");

    ASSERT_EQ(parts.size(), 4u);
    EXPECT_EQ(parts[0], L"a");
    EXPECT_TRUE(parts[1].empty());
    EXPECT_EQ(parts[2], L"b");
    EXPECT_TRUE(parts[3].empty());
}
