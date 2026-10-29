#ifndef APPBOX_LAUNCHER_CONTROL_SHUTDOWNRETRY_HPP
#define APPBOX_LAUNCHER_CONTROL_SHUTDOWNRETRY_HPP

#include <cstdint>

namespace appbox
{

/**
 * @brief Time a second instance keeps racing for the pipe of a closing sandbox.
 *
 * The master instance of a sandbox stops serving its control pipe when its last
 * program exited, so a request which arrives in that window finds a pipe which
 * still exists but answers no more, or which disappears between the race and the
 * connection. The client races again until this budget is used up, because the
 * sandbox it wants to start is still a sandbox which may be started: the request
 * is only reported as failed when no instance of the sandbox can serve it.
 */
constexpr std::uint32_t kShutdownRaceBudgetMs = 30000;

/** @brief Wait before the first race which follows a refused handshake. */
constexpr std::uint32_t kShutdownRaceFirstDelayMs = 200;

/** @brief Upper bound of the wait between two races of one budget. */
constexpr std::uint32_t kShutdownRaceMaxDelayMs = 2000;

/**
 * @brief State of one retry of the race for the control pipe of a sandbox.
 *
 * The wait doubles after every failed attempt and is bounded by
 * `kShutdownRaceMaxDelayMs`, so a sandbox which closes does not cost a client
 * more than the budget and a sandbox which opens again is reached quickly.
 */
struct ShutdownRetry
{
    std::uint32_t delay_ms = 0;      /*!< Wait before the next race, zero when the budget is used up. */
    std::uint32_t remaining_ms = 0;  /*!< Budget which is left when the wait is over. */
    std::uint32_t next_delay_ms = 0; /*!< Wait which follows the next refused handshake. */
};

/**
 * @brief Compute the retry which follows a refused handshake.
 *
 * The call is the whole policy of the shutdown race, so a unit test pins the
 * numbers without a pipe: a wait which does not fit into the budget is not
 * taken at all, and the budget which is left is what the following attempts
 * share.
 *
 * @param[in] remaining_ms Budget which is left, in milliseconds.
 * @param[in] delay_ms Wait of the attempt which was just refused.
 * @return The wait before the next race, the budget which is left afterwards and
 *         the wait which follows it; a `delay_ms` of zero means that the budget
 *         is used up and the request has to be reported as failed.
 */
inline ShutdownRetry NextShutdownRetry(std::uint32_t remaining_ms, std::uint32_t delay_ms)
{
    ShutdownRetry retry;

    if (remaining_ms == 0 || delay_ms == 0 || delay_ms > remaining_ms)
    {
        return retry;
    }

    retry.delay_ms = delay_ms;
    retry.remaining_ms = remaining_ms - delay_ms;

    /* The wait doubles, but a budget which is nearly used up is not wasted. */
    std::uint32_t next = delay_ms * 2;
    if (next > kShutdownRaceMaxDelayMs || next < delay_ms)
    {
        next = kShutdownRaceMaxDelayMs;
    }

    retry.next_delay_ms = next;
    return retry;
}

} // namespace appbox

#endif // APPBOX_LAUNCHER_CONTROL_SHUTDOWNRETRY_HPP
