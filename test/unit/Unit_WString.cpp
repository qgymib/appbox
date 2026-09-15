#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include "WString.hpp"

/**
 * @brief A null narrow string cannot be converted, the conversion helper has to
 *        report the failure instead of allocating a zero sized buffer and
 *        reading past it.
 */
TEST(UnitWString, Utf8ToWideNullInputThrows)
{
    EXPECT_THROW(appbox::UTF8ToWide(static_cast<const char*>(nullptr)), std::runtime_error);
}

/**
 * @brief A null wide string cannot be converted, the conversion helper has to
 *        report the failure instead of allocating a zero sized buffer and
 *        reading past it.
 */
TEST(UnitWString, WideToUTF8NullInputThrows)
{
    EXPECT_THROW(appbox::WideToUTF8(static_cast<const wchar_t*>(nullptr)), std::runtime_error);
}

/**
 * @brief Round trip a plain ASCII string, the behaviour of the conversion
 *        helpers for valid input must not change.
 */
TEST(UnitWString, RoundTripAscii)
{
    const std::string utf8 = "appbox-\\??\\C:\\Users\\foo";
    EXPECT_EQ(appbox::WideToUTF8(appbox::UTF8ToWide(utf8)), utf8);
}

/**
 * @brief Round trip a non ASCII string, the helpers are documented to use the
 *        UTF-8 code page.
 */
TEST(UnitWString, RoundTripNonAscii)
{
    const std::string utf8 = "\xE6\xB2\x99\xE7\xAE\xB1"; /* "沙箱" in UTF-8 */
    EXPECT_EQ(appbox::WideToUTF8(appbox::UTF8ToWide(utf8)), utf8);
}
