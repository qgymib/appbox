#ifndef APPBOX_TEST_UTILS_RUNSTATE_HPP
#define APPBOX_TEST_UTILS_RUNSTATE_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "launcher/msg/RunState.hpp"

namespace appbox::test
{

/** Budget of one state query of a case, in milliseconds. */
constexpr std::uint32_t kRunStateTimeoutMs = 30000;

/** Budget of a case which waits for a state of a run, in milliseconds. */
constexpr std::uint32_t kRunStateWaitTimeoutMs = 30000;

/** Delay between two queries of a wait for a state, in milliseconds. */
constexpr std::uint32_t kRunStatePollMs = 5;

class RawControlPipe;

/**
 * @brief Client which reads the state of a run over the control pipe of its sandbox.
 *
 * The state of a run is what the master instance reports about itself: the stage
 * it reached, the startup screen of the session, the programs it started and
 * what it did with the requests of the other launchers (see
 * `appbox::MsgRunState`). A case which observes a run through the state reads a
 * fact of the protocol instead of a line of a log, which the run writes for a
 * reader of a failed artifact only.
 *
 * The client keeps one connection open, so a wait for a state polls the run over
 * the connection it already has instead of connecting for every query.
 */
class RunStateClient
{
public:
    /**
     * @brief Connect to the control pipe of a sandbox.
     * @param[in] sandbox_id Identifier of the sandbox of the run.
     */
    explicit RunStateClient(const std::string& sandbox_id);
    ~RunStateClient();

    RunStateClient(const RunStateClient&) = delete;
    RunStateClient& operator=(const RunStateClient&) = delete;

    /**
     * @brief Whether the control pipe of the sandbox was opened.
     * @return true when the client can send a query.
     */
    bool IsOpen() const;

    /**
     * @brief Read the state of the run.
     *
     * The call fails for a run which answers with a refusal, because a refusal
     * carries no state: a case which reads the zero of every member of a refusal
     * as a fact of the run would pass for the wrong reason.
     *
     * @param[out] state The state of the run, only valid when the call succeeds.
     * @param[in] timeout_ms Budget of the query.
     * @return true when the run answered with its state.
     */
    bool Query(appbox::MsgRunState::Rsp& state, std::uint32_t timeout_ms = kRunStateTimeoutMs);

    /**
     * @brief Wait until the state of the run satisfies a condition.
     *
     * The call queries the run again after every poll interval and returns as
     * soon as the condition holds, so a case which waits for a stage of the run
     * (a program which exited, for example) needs no fixed delay: the wait
     * covers the budget and reports whether the condition was reached.
     *
     * @tparam Predicate Callable which reads a state and returns whether it is
     *                   the state the case waits for.
     * @param[in] predicate Condition to wait for.
     * @param[out] state The last state the run answered, valid when the call
     *                   succeeds.
     * @param[in] timeout_ms Budget of the wait.
     * @return true when the condition held within the budget.
     */
    template <typename Predicate>
    bool WaitFor(const Predicate& predicate, appbox::MsgRunState::Rsp& state,
                 std::uint32_t timeout_ms = kRunStateWaitTimeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        for (;;)
        {
            if (Query(state) && predicate(state))
            {
                return true;
            }

            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(kRunStatePollMs));
        }
    }

    /**
     * @brief Get the Win32 code of the last failed operation.
     * @return The error code, zero when no operation failed.
     */
    std::uint32_t LastError() const;

private:
    std::unique_ptr<RawControlPipe> pipe_;    /* Connection to the control pipe of the sandbox. */
    std::uint64_t                   next_id_; /* Id of the next query of the client. */
};

/**
 * @brief Read the state of a run of one sandbox.
 *
 * The call connects, queries once and releases the connection, which is what a
 * case needs which reads a single state.
 *
 * @param[in] sandbox_id Identifier of the sandbox of the run.
 * @param[out] state The state of the run, only valid when the call succeeds.
 * @param[in] timeout_ms Budget of the query.
 * @return true when the run answered with its state.
 */
bool QueryRunState(const std::string& sandbox_id, appbox::MsgRunState::Rsp& state,
                   std::uint32_t timeout_ms = kRunStateTimeoutMs);

/**
 * @brief Get the programs a run created for one request.
 * @param[in] state State of the run.
 * @return The programs of the request, in the order the run created them.
 */
std::vector<appbox::MsgRunState::Program> RequestPrograms(const appbox::MsgRunState::Rsp& state);

/**
 * @brief Count the programs of a run which still work.
 *
 * A program which the run created and which neither exited nor was terminated
 * by a rollback is a program which is still alive.
 *
 * @param[in] state State of the run.
 * @return The number of programs which still work.
 */
std::size_t RunningPrograms(const appbox::MsgRunState::Rsp& state);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_RUNSTATE_HPP
