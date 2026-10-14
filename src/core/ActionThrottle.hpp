#ifndef APPBOX_PACKER_CORE_ACTION_THROTTLE_HPP
#define APPBOX_PACKER_CORE_ACTION_THROTTLE_HPP

#include <chrono>
#include <functional>

namespace appbox
{

/**
 * @brief Rate gate which lets at most one update pass per interval.
 *
 * A dialog which reports a long action is updated by the main window through
 * messages which arrive far more often than a user can read them, so the gate
 * decides whether an update may be applied right now: the first update of a
 * fresh gate passes and every further one has to wait until the interval has
 * passed since the last update which passed. An update which is refused does
 * not shift the window of the next one, so a burst of reports cannot delay
 * the update which would pass next.
 *
 * The gate works on the steady clock and the clock is injected, so the tests
 * verify the exact boundary of the interval instead of sleeping. The gate
 * itself carries no state which another thread would touch: the dialog calls
 * it from the main thread only.
 */
class ActionThrottle
{
public:
    /**
     * @brief Clock the gate reads the time through.
     */
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    /**
     * @brief Create a gate.
     *
     * @param[in] interval Time which has to pass between two updates which
     *                     pass; a non positive interval lets every update
     *                     pass, so a caller which wants no throttling keeps
     *                     the same calling code.
     * @param[in] clock Clock to read the time from; an empty clock reads the
     *                  steady clock of the system.
     */
    explicit ActionThrottle(std::chrono::milliseconds interval, const Clock& clock = Clock());

    /**
     * @brief Ask whether an update may be applied now.
     *
     * @return true when the update passes; the call then starts a new
     *         interval. false when the update has to be kept and retried
     *         later.
     */
    bool TryAcquire();

private:
    /**
     * @brief Time which has to pass between two updates which pass.
     */
    std::chrono::milliseconds interval_;

    /**
     * @brief Clock the gate reads the time through, never empty.
     */
    Clock clock_;

    /**
     * @brief Whether an update has passed already.
     */
    bool has_last_ = false;

    /**
     * @brief Time of the update which passed last.
     */
    std::chrono::steady_clock::time_point last_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_ACTION_THROTTLE_HPP
