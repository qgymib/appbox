#include <gtest/gtest.h>
#include "src/core/ActionThrottle.hpp"
#include <chrono>
#include <functional>

namespace
{

/**
 * @brief Deterministic clock of the tests.
 *
 * The throttle works on the steady clock, so the tests move the clock by hand
 * instead of sleeping: the behaviour is verified at the exact interval
 * boundary and not after some larger delay.
 */
class FakeClock
{
public:
    /**
     * @brief Build the clock callback the throttle reads the time through.
     * @return The callback, which returns the time the fake clock holds.
     */
    appbox::ActionThrottle::Clock Callback()
    {
        return [this]() { return now; };
    }

    /**
     * @brief Move the clock forward.
     * @param[in] offset Offset to add to the current time.
     */
    void Advance(std::chrono::milliseconds offset)
    {
        now += offset;
    }

    std::chrono::steady_clock::time_point now{ std::chrono::steady_clock::time_point::min() };
};

} // namespace

/**
 * @brief The first update of a fresh gate passes immediately.
 */
TEST(Unit_ActionThrottle, FirstUpdatePasses)
{
    FakeClock              clock;
    appbox::ActionThrottle throttle(std::chrono::milliseconds(100), clock.Callback());

    EXPECT_TRUE(throttle.TryAcquire());
}

/**
 * @brief An update which arrives before the interval has passed is refused,
 *        so a caller which reports ten times a second applies at most one
 *        update per hundred milliseconds.
 */
TEST(Unit_ActionThrottle, UpdateInsideTheIntervalIsRefused)
{
    FakeClock              clock;
    appbox::ActionThrottle throttle(std::chrono::milliseconds(100), clock.Callback());

    ASSERT_TRUE(throttle.TryAcquire());

    clock.Advance(std::chrono::milliseconds(1));
    EXPECT_FALSE(throttle.TryAcquire());

    clock.Advance(std::chrono::milliseconds(98));
    EXPECT_FALSE(throttle.TryAcquire());
}

/**
 * @brief The interval boundary itself passes, so an update is applied again
 *        after the whole interval and not only after more than it.
 */
TEST(Unit_ActionThrottle, UpdateAtTheIntervalBoundaryPasses)
{
    FakeClock              clock;
    appbox::ActionThrottle throttle(std::chrono::milliseconds(100), clock.Callback());

    ASSERT_TRUE(throttle.TryAcquire());

    clock.Advance(std::chrono::milliseconds(100));
    EXPECT_TRUE(throttle.TryAcquire());
}

/**
 * @brief A refused update does not shift the window of the next one.
 *
 * The gate measures the time since the last update which passed, so a burst of
 * refused updates in between cannot delay the update which would pass next.
 */
TEST(Unit_ActionThrottle, RefusedUpdatesDoNotShiftTheWindow)
{
    FakeClock              clock;
    appbox::ActionThrottle throttle(std::chrono::milliseconds(100), clock.Callback());

    ASSERT_TRUE(throttle.TryAcquire());

    clock.Advance(std::chrono::milliseconds(99));
    ASSERT_FALSE(throttle.TryAcquire());

    clock.Advance(std::chrono::milliseconds(1));
    EXPECT_TRUE(throttle.TryAcquire());
}

/**
 * @brief The interval of the gate is the one it was created with.
 */
TEST(Unit_ActionThrottle, GateUsesItsOwnInterval)
{
    FakeClock              clock;
    appbox::ActionThrottle throttle(std::chrono::milliseconds(250), clock.Callback());

    ASSERT_TRUE(throttle.TryAcquire());

    clock.Advance(std::chrono::milliseconds(100));
    EXPECT_FALSE(throttle.TryAcquire());

    clock.Advance(std::chrono::milliseconds(149));
    EXPECT_FALSE(throttle.TryAcquire());

    clock.Advance(std::chrono::milliseconds(1));
    EXPECT_TRUE(throttle.TryAcquire());
}

/**
 * @brief A gate without an interval passes every update.
 *
 * A caller which wants no throttling creates the gate with a non positive
 * interval instead of bypassing it, so the calling code stays uniform.
 */
TEST(Unit_ActionThrottle, NonPositiveIntervalPassesEveryUpdate)
{
    FakeClock              clock;
    appbox::ActionThrottle throttle(std::chrono::milliseconds(0), clock.Callback());

    EXPECT_TRUE(throttle.TryAcquire());

    clock.Advance(std::chrono::milliseconds(1));
    EXPECT_TRUE(throttle.TryAcquire());
    EXPECT_TRUE(throttle.TryAcquire());
}

/**
 * @brief The gate without a clock reads the steady clock of the system.
 *
 * The gate must not crash and must pass the first update when it decides on
 * its own when the time is, which is how the dialog uses it.
 */
TEST(Unit_ActionThrottle, DefaultClockPassesTheFirstUpdate)
{
    appbox::ActionThrottle throttle(std::chrono::milliseconds(100));

    EXPECT_TRUE(throttle.TryAcquire());
}
