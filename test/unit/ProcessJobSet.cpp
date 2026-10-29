#include "utils/ProcessJobSet.hpp" /* Must be first include file */
#include <gtest/gtest.h>
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

/**
 * @brief Build a job of the set which never started a process.
 *
 * The job reports to the completion port of the set but no process ever
 * reports for it, which is the state a wait has to time out on.
 *
 * @param[in] set Set the job belongs to.
 * @return The job.
 */
std::unique_ptr<appbox::ProcessJob> MakeUnstartedJob(appbox::ProcessJobSet& set)
{
    appbox::SandboxConfig cfg;

    return std::make_unique<appbox::ProcessJob>(L"appbox_unit_missing.exe", L"appbox_unit_missing.exe",
                                                std::vector<std::wstring>(), cfg, false, set.CompletionPort());
}

/**
 * @brief Take a job which never started a process into the set.
 *
 * @param[in,out] set Set which receives the job.
 * @param[in] origin Why the job exists.
 * @param[in] request_id Request the job belongs to.
 */
void AdoptUnstartedJob(appbox::ProcessJobSet& set, appbox::JobOrigin origin, std::uint64_t request_id)
{
    set.Adopt(MakeUnstartedJob(set), origin, request_id);
}

} // namespace

/**
 * @brief A set without a job is complete at once.
 */
TEST(Unit_ProcessJobSet, EmptySetReportsSuccess)
{
    appbox::ProcessJobSet set;

    EXPECT_EQ(set.Size(), 0u);

    const auto result = set.WaitAll(0);
    EXPECT_EQ(result.status, appbox::WaitStatus::AllExited);
    EXPECT_EQ(result.error, 0u);
}

/**
 * @brief A job which never reports keeps the wait from succeeding.
 *
 * The wait reports the timeout instead of a successful wait, because a job of
 * the set did not report that its processes exited.
 */
TEST(Unit_ProcessJobSet, JobWhichNeverReportsTimesOut)
{
    appbox::ProcessJobSet set;
    set.Adopt(MakeUnstartedJob(set));

    ASSERT_EQ(set.Size(), 1u);
    ASSERT_NE(set.Job(0), nullptr);

    EXPECT_EQ(set.WaitAll(0).status, appbox::WaitStatus::TimedOut);
    EXPECT_EQ(set.WaitAll(50).status, appbox::WaitStatus::TimedOut);
}

/**
 * @brief A job which joins while the set waits is covered by the wait.
 *
 * The wait which already covers a job wakes up for the job which joined, takes
 * it into account and only reports the timeout of its own deadline: a request
 * of another launcher which arrives while the run waits is not lost.
 */
TEST(Unit_ProcessJobSet, JobAdoptedWhileWaitingIsCoveredByTheWait)
{
    appbox::ProcessJobSet set;

    /* The wait has something to wait for, so it does not return at once. */
    set.Adopt(MakeUnstartedJob(set));

    auto job = MakeUnstartedJob(set);

    std::atomic<int> status(-1);
    std::thread      waiter([&set, &status]() { status.store(static_cast<int>(set.WaitAll(300).status)); });

    Sleep(50);
    set.Adopt(std::move(job));
    waiter.join();

    EXPECT_EQ(set.Size(), 2u);
    EXPECT_EQ(status.load(), static_cast<int>(appbox::WaitStatus::TimedOut));
}

/**
 * @brief A wakeup interrupts the wait at once.
 *
 * The control thread of a run waits for the processes of the run, so a request
 * of another launcher has to end the wait instead of waiting for the next
 * timeout: the wait reports the wakeup long before its own budget is used up.
 */
TEST(Unit_ProcessJobSet, WakeInterruptsWait)
{
    appbox::ProcessJobSet set;
    set.Adopt(MakeUnstartedJob(set));

    std::atomic<int> status(-1);
    std::thread      waiter([&set, &status]() { status.store(static_cast<int>(set.WaitAll(5000).status)); });

    /* The wait has to be blocked before it can be woken up. */
    Sleep(50);

    const ULONGLONG started = GetTickCount64();
    set.Wake();
    waiter.join();
    const ULONGLONG elapsed = GetTickCount64() - started;

    EXPECT_EQ(status.load(), static_cast<int>(appbox::WaitStatus::Woken));
    EXPECT_LT(elapsed, static_cast<ULONGLONG>(2000)) << "the wait was not woken up by the call";
}

/**
 * @brief A wakeup is no completion of a set which holds no job.
 *
 * The wakeup says that the caller has work to do and nothing about the
 * processes of the set, so a set without a job stays complete.
 */
TEST(Unit_ProcessJobSet, WakeWithoutJobKeepsWaiting)
{
    appbox::ProcessJobSet set;
    set.Wake();

    EXPECT_EQ(set.WaitAll(0).status, appbox::WaitStatus::AllExited);
    EXPECT_EQ(set.WaitAll(0).status, appbox::WaitStatus::AllExited);
}

/**
 * @brief The wait tells the end of the processes apart from a wakeup.
 *
 * A run ends when every job reported that its processes exited, while a wakeup
 * only asks the caller to handle a request: the two outcomes have to be
 * distinguishable, because the run would otherwise end with a request pending.
 */
TEST(Unit_ProcessJobSet, WaitResultDistinguishesExitFromWake)
{
    appbox::ProcessJobSet finished;
    EXPECT_EQ(finished.WaitAll(1000).status, appbox::WaitStatus::AllExited);

    appbox::ProcessJobSet running;
    running.Adopt(MakeUnstartedJob(running));
    running.Wake();

    EXPECT_EQ(running.WaitAll(1000).status, appbox::WaitStatus::Woken);

    /* The wakeup is taken, the job still did not report its exit. */
    EXPECT_EQ(running.WaitAll(0).status, appbox::WaitStatus::TimedOut);
}

/**
 * @brief The set records why a job exists and which request it belongs to.
 *
 * The origin decides whether a job contributes to the exit code of the run,
 * and the request id decides which jobs a rollback terminates, so the two
 * values have to survive the accounting of the set.
 */
TEST(Unit_ProcessJobSet, RecordsJobOrigin)
{
    appbox::ProcessJobSet set;
    AdoptUnstartedJob(set, appbox::JobOrigin::Initial, 0);
    AdoptUnstartedJob(set, appbox::JobOrigin::Request, 7);

    ASSERT_EQ(set.Size(), 2u);

    EXPECT_TRUE(set.IsInitial(0));
    EXPECT_EQ(set.RequestIdOf(0), 0u);

    EXPECT_FALSE(set.IsInitial(1));
    EXPECT_EQ(set.RequestIdOf(1), 7u);

    /* An index out of range belongs to no job at all. */
    EXPECT_FALSE(set.IsInitial(2));
    EXPECT_EQ(set.RequestIdOf(2), 0u);
}

/**
 * @brief A rollback terminates the jobs of one request and of no other.
 *
 * The call reports the number of jobs it terminated, so a set which holds a job
 * of the initial run and a job of another request proves that the rollback
 * matches the request id and the origin instead of terminating whatever the set
 * holds. A job which never created a process is terminated as well, which keeps
 * the case free of a real process; the end to end case `AutoRollback` covers
 * the processes which are really killed.
 */
TEST(Unit_ProcessJobSet, TerminateRequestKillsOnlyThatRequest)
{
    appbox::ProcessJobSet set;
    AdoptUnstartedJob(set, appbox::JobOrigin::Initial, 0);
    AdoptUnstartedJob(set, appbox::JobOrigin::Request, 7);
    AdoptUnstartedJob(set, appbox::JobOrigin::Request, 8);

    ASSERT_EQ(set.Size(), 3u);

    EXPECT_EQ(set.TerminateRequest(7), 1u);
    EXPECT_EQ(set.TerminateRequest(8), 1u);

    /* The job of the initial run carries the request id zero, and a request id
     * which names no job terminates nothing. */
    EXPECT_EQ(set.TerminateRequest(0), 0u);
    EXPECT_EQ(set.TerminateRequest(9), 0u);
}
