#include <gtest/gtest.h>
#include "src/core/BuildReport.hpp"
#include <chrono>
#include <string>

/**
 * @brief The elapsed time is shown as mm:ss and switches to h:mm:ss only for
 *        runs which last an hour or longer.
 */
TEST(Unit_BuildReport, ElapsedTimeUsesMinuteSecondFormat)
{
    using std::chrono::milliseconds;

    EXPECT_EQ(appbox::FormatElapsed(milliseconds(0)), "00:00");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(12 * 1000)), "00:12");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(59999)), "00:59");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(61 * 1000)), "01:01");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(3600 * 1000)), "1:00:00");
    EXPECT_EQ(appbox::FormatElapsed(milliseconds(3661 * 1000)), "1:01:01");
}

/**
 * @brief A negative duration cannot happen in a run but must not produce a
 *        broken text either.
 */
TEST(Unit_BuildReport, ElapsedTimeTreatsNegativeValuesAsZero)
{
    EXPECT_EQ(appbox::FormatElapsed(std::chrono::milliseconds(-5000)), "00:00");
}
