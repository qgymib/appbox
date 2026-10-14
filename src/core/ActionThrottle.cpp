#include "core/ActionThrottle.hpp"

namespace appbox
{

namespace
{

/**
 * @brief Read the time from the steady clock of the system.
 * @return The current time of the monotonic clock.
 */
std::chrono::steady_clock::time_point SystemNow()
{
    return std::chrono::steady_clock::now();
}

} // namespace

ActionThrottle::ActionThrottle(std::chrono::milliseconds interval, const Clock& clock)
    : interval_(interval), clock_(clock ? clock : Clock(SystemNow))
{
}

bool ActionThrottle::TryAcquire()
{
    const auto now = clock_();

    if (has_last_ && interval_ > std::chrono::milliseconds::zero() && now - last_ < interval_)
    {
        return false;
    }

    has_last_ = true;
    last_ = now;
    return true;
}

} // namespace appbox
