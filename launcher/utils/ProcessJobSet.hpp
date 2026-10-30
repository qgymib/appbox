#ifndef APPBOX_LAUNCHER_UTILS_PROCESSJOBSET_HPP
#define APPBOX_LAUNCHER_UTILS_PROCESSJOBSET_HPP

#include "WinAPI.h"
#include "sandbox/Config.hpp"
#include "ProcessJob.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Why a job of a set exists.
 *
 * The origin of a job decides what it contributes to the run: only the
 * programs of the initial run decide the exit code of the launcher, while the
 * programs of a request are the work of another launcher and are rolled back
 * with the request which asked for them.
 */
enum class JobOrigin
{
    Initial, /*!< The programs the configuration of the run describes. */
    Request, /*!< The programs a start request of another launcher asked for. */
};

/**
 * @brief Outcome of one wait for the processes of a set.
 */
enum class WaitStatus
{
    AllExited, /*!< Every job of the set reported that its processes exited. */
    Woken,     /*!< The wait was woken up, the caller has work to do. */
    TimedOut,  /*!< The timeout elapsed while a job still held processes. */
    Error,     /*!< The wait itself failed, see WaitResult::error. */
};

/**
 * @brief Result of one wait.
 *
 * A wait reports why it returned instead of folding every outcome into one
 * error code: a run which was woken up has to handle the request of another
 * launcher, while a run whose processes all exited may end.
 */
struct WaitResult
{
    WaitStatus status = WaitStatus::AllExited; /*!< Why the wait returned. */
    DWORD      error = 0;                      /*!< Error code of the wait, zero unless the status is Error. */
};

/**
 * @brief One job of a set, as a snapshot of the set describes it.
 *
 * The entry is what the state query of a run reports about one program: the
 * process the job created, the origin of the job, the request which asked for
 * it, whether it exited and with which code, and whether a rollback terminated
 * it. It describes the accounting of the run and not a line of a log.
 */
struct JobState
{
    std::uint32_t pid = 0;                     /*!< Process id of the created process. */
    JobOrigin     origin = JobOrigin::Initial; /*!< Why the job exists. */
    std::uint64_t request_id = 0;              /*!< Request the job belongs to, zero for the initial run. */
    bool          exited = false;              /*!< True once the job reported that its processes exited. */
    bool          terminated = false;          /*!< True when a rollback of its request terminated the job. */
    DWORD         exit_code = 0;               /*!< Exit code, meaningful only while `exited` is set. */
};

/**
 * @brief The processes one run of the launcher starts, and the wait for all of them.
 *
 * Every job of the set reports to one IO completion port, so a run waits for
 * all of its processes with one call instead of one wait per process: the wait
 * is event driven and returns as soon as the last job reported that its
 * processes exited.
 *
 * A job can join the set while the set waits. The launcher of a sandbox starts
 * the programs of a request while it already waits for the programs it started
 * itself, see `docs/Launcher.md`, so the wait covers a job which joins later
 * instead of waiting for a fixed list of jobs.
 *
 * A request which arrives while the run waits wakes the wait up with Wake(), so
 * the control thread handles it at once instead of polling. A wakeup says
 * nothing about the processes of the set: the wait reports Woken and the caller
 * decides whether the run may end.
 *
 * The jobs are held in the order they were started, which is the order a run
 * reports their exit codes in.
 */
struct ProcessJobSet
{
    ProcessJobSet();
    ~ProcessJobSet();

    ProcessJobSet(const ProcessJobSet&) = delete;
    ProcessJobSet& operator=(const ProcessJobSet&) = delete;

    /**
     * @brief Get the completion port the jobs of the set report to.
     *
     * A caller which builds a job of its own hands the port to the job and
     * takes the job into the set with Adopt().
     *
     * @return The port, null when it could not be created.
     */
    HANDLE CompletionPort() const;

    /**
     * @brief Create and start one process, and take it into the set.
     *
     * A process which cannot be created is not taken into the set, so the wait
     * of the run never waits for a process which does not exist.
     *
     * @param[in] image_path Host path of the image to create.
     * @param[in] command_line_path Path the command line names as its program.
     * @param[in] args Arguments of the program.
     * @param[in] inject_data Injection data written into the target process.
     * @param[in] hide_console True to start the target without a console window.
     * @param[in] options Environment and working directory of the process, see
     *                    ProcessJobOptions.
     * @param[in] origin Why the process is started, see JobOrigin.
     * @param[in] request_id Request the process belongs to, zero for a job of
     *                       the initial run.
     * @return 0 when the process was created, otherwise the error code of the
     *         creation.
     */
    DWORD Start(const std::wstring& image_path, const std::wstring& command_line_path,
                const std::vector<std::wstring>& args, const appbox::SandboxConfig& inject_data, bool hide_console,
                const appbox::ProcessJobOptions& options = {}, JobOrigin origin = JobOrigin::Initial,
                std::uint64_t request_id = 0);

    /**
     * @brief Take a job into the set.
     *
     * The job has to report to the completion port of the set. A job which the
     * caller started itself is waited for from the moment it joins, and a job
     * whose processes already exited does not keep the wait from succeeding.
     *
     * @param[in] job The job, which the set owns from now on.
     * @param[in] origin Why the job exists, see JobOrigin.
     * @param[in] request_id Request the job belongs to, zero for a job of the
     *                       initial run.
     */
    void Adopt(std::unique_ptr<appbox::ProcessJob> job, JobOrigin origin = JobOrigin::Initial,
               std::uint64_t request_id = 0);

    /**
     * @brief Wait until every job of the set reported that its processes exited.
     *
     * The call also returns when the wait is woken up by Wake(), which is how
     * the control thread of a run learns that a request waits for it.
     *
     * @param[in] timeout Timeout in milliseconds, INFINITE to wait without one.
     * @return Why the wait returned and, for a failing wait, its error code.
     */
    WaitResult WaitAll(DWORD timeout);

    /**
     * @brief Wake up a wait of the set.
     *
     * The wakeup interrupts WaitAll() with WaitStatus::Woken, which is what a
     * request of another launcher asks for: the control thread handles the
     * request instead of waiting for the next timeout. A wakeup which is
     * already pending is not posted twice, so a burst of requests wakes the
     * wait up once.
     *
     * A wakeup which arrives before the wait starts is kept: the wait which
     * follows reports it instead of losing it.
     */
    void Wake();

    /**
     * @brief Terminate every process a request started.
     *
     * The call is the rollback of a request: a request whose programs cannot
     * all be created leaves no process behind. The jobs stay in the set, so the
     * wait of the run still counts their exit report and the accounting of the
     * run stays correct.
     *
     * Only the jobs of the request are terminated: the programs of the initial
     * run and the programs of another request are not touched.
     *
     * @param[in] request_id Request whose processes are terminated.
     * @return The number of jobs which were terminated.
     */
    std::size_t TerminateRequest(std::uint64_t request_id);

    /**
     * @brief Describe every job of the set.
     *
     * The call copies what the set accounts for, so the caller reads the state
     * of the run without holding the lock of the set: a reader of the state
     * query runs on the RPC thread while the control thread starts and waits
     * for the processes. A job which did not report its end carries no exit
     * code, because the system reports a running process as still active.
     *
     * @param[out] jobs The description of the jobs, in the order they joined.
     */
    void Snapshot(std::vector<JobState>& jobs);

    /**
     * @brief Get the number of jobs of the set.
     * @return The number of jobs, including the ones which already exited.
     */
    std::size_t Size() const;

    /**
     * @brief Get one job of the set.
     * @param[in] index Index of the job, in the order the set started them.
     * @return The job, null when the index is out of range.
     */
    appbox::ProcessJob* Job(std::size_t index) const;

    /**
     * @brief Report whether a job belongs to the initial run.
     * @param[in] index Index of the job, in the order the set started them.
     * @return true when the job was started for the configuration of the run,
     *         false for a job of a request and for an index out of range.
     */
    bool IsInitial(std::size_t index) const;

    /**
     * @brief Get the request a job belongs to.
     * @param[in] index Index of the job, in the order the set started them.
     * @return The request id, zero for a job of the initial run and for an
     *         index out of range.
     */
    std::uint64_t RequestIdOf(std::size_t index) const;

    struct Data;
    Data* data_;
};

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_PROCESSJOBSET_HPP
