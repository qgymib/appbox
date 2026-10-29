#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "control/ShutdownRetry.hpp"

/**
 * @brief The wait of the shutdown race starts at the first delay, doubles and
 *        stops at the upper bound, so a closing sandbox is retried quickly at
 *        first and slowly afterwards.
 */
TEST(Unit_ShutdownRetry, DoublesUntilTheCap)
{
    std::vector<std::uint32_t> waits;
    std::uint32_t              remaining = appbox::kShutdownRaceBudgetMs;
    std::uint32_t              delay = appbox::kShutdownRaceFirstDelayMs;

    for (;;)
    {
        const auto retry = appbox::NextShutdownRetry(remaining, delay);
        if (retry.delay_ms == 0)
        {
            break;
        }

        waits.push_back(retry.delay_ms);
        EXPECT_EQ(retry.remaining_ms, remaining - retry.delay_ms);

        remaining = retry.remaining_ms;
        delay = retry.next_delay_ms;
    }

    const std::vector<std::uint32_t> expected = { 200, 400, 800, 1600, 2000, 2000, 2000 };
    ASSERT_GE(waits.size(), expected.size()) << waits.size();

    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(waits[index], expected[index]) << index;
    }

    /* The waits never exceed the budget which is left. */
    EXPECT_LT(remaining, appbox::kShutdownRaceMaxDelayMs);
}

/**
 * @brief A wait which does not fit into the budget is not taken at all: the
 *        request is reported as failed instead of being retried for a time the
 *        client did not agree to.
 */
TEST(Unit_ShutdownRetry, WaitWhichDoesNotFitEndsTheRetries)
{
    const auto retry = appbox::NextShutdownRetry(500, 800);

    EXPECT_EQ(retry.delay_ms, 0u);
    EXPECT_EQ(retry.remaining_ms, 0u);
}

/**
 * @brief A budget of zero and a delay of zero end the retries, which is what a
 *        client does once it gave up.
 */
TEST(Unit_ShutdownRetry, EmptyBudgetEndsTheRetries)
{
    EXPECT_EQ(appbox::NextShutdownRetry(0, 200).delay_ms, 0u);
    EXPECT_EQ(appbox::NextShutdownRetry(1000, 0).delay_ms, 0u);
}

/**
 * @brief The wait which follows the next attempt is capped, so a long budget
 *        does not grow the interval without a bound.
 */
TEST(Unit_ShutdownRetry, NextWaitIsCapped)
{
    const auto retry = appbox::NextShutdownRetry(appbox::kShutdownRaceBudgetMs, appbox::kShutdownRaceMaxDelayMs);

    EXPECT_EQ(retry.delay_ms, appbox::kShutdownRaceMaxDelayMs);
    EXPECT_EQ(retry.next_delay_ms, appbox::kShutdownRaceMaxDelayMs);
    EXPECT_EQ(retry.remaining_ms, appbox::kShutdownRaceBudgetMs - appbox::kShutdownRaceMaxDelayMs);
}
