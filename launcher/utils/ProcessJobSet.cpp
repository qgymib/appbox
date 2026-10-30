#include "WinAPI.h" /* Must be first include file */
#include <spdlog/spdlog.h>
#include <atomic>
#include <mutex>
#include "ProcessJobSet.hpp"

namespace
{

/**
 * @brief Key of the completion message which reports that a job joined the set.
 *
 * The key of a message of a job is the handle of its job object, which is never
 * zero, so zero names no job and marks the messages the set posts itself.
 */
constexpr ULONG_PTR kJobJoinedKey = 0;

/**
 * @brief Key of the completion message which wakes a wait of the set up.
 *
 * The key of a job message is a handle of a job object, and a handle is a
 * multiple of four, so a key of one names no job either.
 */
constexpr ULONG_PTR kWakeKey = 1;

/**
 * @brief Exit code of the processes a rollback terminated.
 *
 * The code is the one the system reports for a cancelled operation, which is
 * what a terminated request is. The processes of a request never decide the
 * exit code of the run, so the value is reported by the state query of the run
 * and by the log of the rollback alone.
 */
constexpr DWORD kTerminatedExitCode = ERROR_CANCELLED;

/**
 * @brief One job of the set and the state of its report.
 */
struct JobEntry
{
    std::unique_ptr<appbox::ProcessJob> job;        /* Job of the entry. */
    bool                                reported;   /* True once the job reported that it is empty. */
    bool                                terminated; /* True once a rollback terminated the job. */
    appbox::JobOrigin                   origin;     /* Why the job exists. */
    std::uint64_t                       request_id; /* Request the job belongs to, zero for the initial run. */
};

/**
 * @brief Report whether a job has no process left.
 *
 * A job which never created a process is empty as well, so the accounting tells
 * the two states apart: a job which counted no process at all is not waited
 * for, while a job whose processes all exited does not keep the wait from
 * succeeding.
 *
 * @param[in] job Job to inspect.
 * @return true when the job created at least one process and holds none anymore.
 */
bool IsEmpty(appbox::ProcessJob& job)
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
    ZeroMemory(&accounting, sizeof(accounting));

    if (!QueryInformationJobObject(job.JobHandle(), JobObjectBasicAccountingInformation, &accounting,
                                   sizeof(accounting), nullptr))
    {
        return false;
    }

    return accounting.TotalProcesses > 0 && accounting.ActiveProcesses == 0;
}

} // namespace

struct appbox::ProcessJobSet::Data
{
    HANDLE                iocp = nullptr; /* Completion port every job of the set reports to. */
    std::vector<JobEntry> entries;        /* Jobs of the set, in the order they joined. */
    std::size_t           pending = 0;    /* Number of jobs which have not reported that they are empty. */
    std::atomic<bool>     woken{ false }; /* True while a wakeup waits for the wait to take it. */
    mutable std::mutex    mutex;          /* Guards the members above, which a wait and an add share. */
};

appbox::ProcessJobSet::ProcessJobSet() : data_(new Data())
{
    data_->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (data_->iocp == nullptr)
    {
        SPDLOG_ERROR("failed to create the IO completion port: {}", GetLastError());
    }
}

appbox::ProcessJobSet::~ProcessJobSet()
{
    /* The jobs are released first: a job which is released kills its processes. */
    data_->entries.clear();

    if (data_->iocp != nullptr)
    {
        CloseHandle(data_->iocp);
        data_->iocp = nullptr;
    }

    delete data_;
}

HANDLE appbox::ProcessJobSet::CompletionPort() const
{
    return data_->iocp;
}

DWORD appbox::ProcessJobSet::Start(const std::wstring& image_path, const std::wstring& command_line_path,
                                   const std::vector<std::wstring>& args, const appbox::SandboxConfig& inject_data,
                                   bool hide_console, const appbox::ProcessJobOptions& options, JobOrigin origin,
                                   std::uint64_t request_id)
{
    if (data_->iocp == nullptr)
    {
        SPDLOG_ERROR("the completion port of the set was not created");
        return ERROR_INVALID_HANDLE;
    }

    auto job = std::make_unique<appbox::ProcessJob>(image_path, command_line_path, args, inject_data, hide_console,
                                                    data_->iocp, options);

    const DWORD ret = job->Start();
    if (ret != 0)
    {
        return ret;
    }

    Adopt(std::move(job), origin, request_id);
    return 0;
}

void appbox::ProcessJobSet::Adopt(std::unique_ptr<appbox::ProcessJob> job, JobOrigin origin, std::uint64_t request_id)
{
    if (!job)
    {
        return;
    }

    /*
     * A process which exited before the job joined the set already reported to
     * the port, and the report may have been read before the job was known: the
     * accounting of the job tells whether there is anything left to wait for.
     */
    const bool empty = IsEmpty(*job);

    {
        std::lock_guard<std::mutex> lock(data_->mutex);

        JobEntry entry;
        entry.job = std::move(job);
        entry.reported = empty;
        entry.terminated = false;
        entry.origin = origin;
        entry.request_id = request_id;
        data_->entries.push_back(std::move(entry));

        if (!empty)
        {
            ++data_->pending;
        }
    }

    /*
     * The wait is woken up for the job which joined, so a wait which already
     * covered every job it knew returns only when the job which joined is empty
     * as well.
     */
    if (!PostQueuedCompletionStatus(data_->iocp, 0, kJobJoinedKey, nullptr))
    {
        SPDLOG_WARN("failed to wake up the wait for the processes: {}", GetLastError());
    }
}

appbox::WaitResult appbox::ProcessJobSet::WaitAll(DWORD timeout)
{
    if (data_->iocp == nullptr)
    {
        return WaitResult{ WaitStatus::Error, ERROR_INVALID_HANDLE };
    }

    /* The deadline is kept in ticks, which do not run backwards like the clock does. */
    const ULONGLONG deadline = (timeout == INFINITE) ? 0 : GetTickCount64() + timeout;

    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(data_->mutex);
            if (data_->pending == 0)
            {
                return WaitResult{ WaitStatus::AllExited, 0 };
            }
        }

        /*
         * A wait without time left still reads the messages which are already
         * queued: a wakeup which arrived before the wait started is reported
         * instead of being lost.
         */
        DWORD wait = INFINITE;
        if (timeout != INFINITE)
        {
            const ULONGLONG now = GetTickCount64();
            wait = (now >= deadline) ? 0 : static_cast<DWORD>(deadline - now);
        }

        DWORD        code = 0;
        ULONG_PTR    key = 0;
        LPOVERLAPPED ov = nullptr;
        if (!GetQueuedCompletionStatus(data_->iocp, &code, &key, &ov, wait))
        {
            const DWORD err = GetLastError();

            /*
             * A wait timeout is reported as WAIT_TIMEOUT, the caller expects a
             * result of its own. A failing operation must never be reported as
             * a successful wait.
             */
            if (ov == nullptr && err == WAIT_TIMEOUT)
            {
                {
                    std::lock_guard<std::mutex> lock(data_->mutex);
                    if (data_->pending == 0)
                    {
                        return WaitResult{ WaitStatus::AllExited, 0 };
                    }
                }

                if (timeout != INFINITE && GetTickCount64() >= deadline)
                {
                    return WaitResult{ WaitStatus::TimedOut, 0 };
                }

                continue;
            }

            return WaitResult{ WaitStatus::Error, (err != ERROR_SUCCESS) ? err : ERROR_TIMEOUT };
        }

        if (key == kWakeKey)
        {
            /*
             * The wakeup is taken: the next one is posted again, so a request
             * which arrives after this point wakes the next wait up.
             */
            data_->woken.store(false);
            return WaitResult{ WaitStatus::Woken, 0 };
        }

        if (key == kJobJoinedKey)
        {
            /* A job joined the set: the step above counts it. */
            continue;
        }

        if (code != JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO)
        {
            /* Any other message of a job says nothing about the end of its processes. */
            continue;
        }

        std::lock_guard<std::mutex> lock(data_->mutex);
        for (auto& entry : data_->entries)
        {
            if (entry.job->JobHandle() == reinterpret_cast<HANDLE>(key) && !entry.reported)
            {
                entry.reported = true;
                if (data_->pending > 0)
                {
                    --data_->pending;
                }
                break;
            }
        }
    }
}

void appbox::ProcessJobSet::Wake()
{
    if (data_->iocp == nullptr)
    {
        return;
    }

    /*
     * A wakeup which is already pending covers this request as well: the caller
     * takes the queue of requests as a whole, so one message is enough and a
     * burst of requests does not flood the port.
     */
    if (data_->woken.exchange(true))
    {
        return;
    }

    if (!PostQueuedCompletionStatus(data_->iocp, 0, kWakeKey, nullptr))
    {
        /* The message was not posted, so the next request has to post one again. */
        data_->woken.store(false);
        SPDLOG_WARN("failed to wake up the wait for the processes: {}", GetLastError());
    }
}

std::size_t appbox::ProcessJobSet::TerminateRequest(std::uint64_t request_id)
{
    std::lock_guard<std::mutex> lock(data_->mutex);

    std::size_t terminated = 0;
    for (auto& entry : data_->entries)
    {
        if (entry.origin != JobOrigin::Request || entry.request_id != request_id)
        {
            continue;
        }

        if (entry.job->Terminate(kTerminatedExitCode) != 0)
        {
            continue;
        }

        /*
         * The rollback is recorded on the entry and not read from the exit code
         * of the process later: the termination of a job is asynchronous, so the
         * exit code of a process may still be the one of a running process when
         * the state of the run is read.
         */
        entry.terminated = true;
        ++terminated;
    }

    return terminated;
}

void appbox::ProcessJobSet::Snapshot(std::vector<JobState>& jobs)
{
    std::lock_guard<std::mutex> lock(data_->mutex);

    jobs.clear();
    jobs.reserve(data_->entries.size());

    for (auto& entry : data_->entries)
    {
        JobState state;
        state.pid = static_cast<std::uint32_t>(entry.job->ProcessId());
        state.origin = entry.origin;
        state.request_id = entry.request_id;
        state.exited = entry.reported;
        state.terminated = entry.terminated;
        state.exit_code = entry.reported ? entry.job->GetExitCode() : 0;
        jobs.push_back(state);
    }
}

std::size_t appbox::ProcessJobSet::Size() const
{
    std::lock_guard<std::mutex> lock(data_->mutex);
    return data_->entries.size();
}

appbox::ProcessJob* appbox::ProcessJobSet::Job(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(data_->mutex);
    if (index >= data_->entries.size())
    {
        return nullptr;
    }

    return data_->entries[index].job.get();
}

bool appbox::ProcessJobSet::IsInitial(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(data_->mutex);
    if (index >= data_->entries.size())
    {
        return false;
    }

    return data_->entries[index].origin == JobOrigin::Initial;
}

std::uint64_t appbox::ProcessJobSet::RequestIdOf(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(data_->mutex);
    if (index >= data_->entries.size())
    {
        return 0;
    }

    return data_->entries[index].request_id;
}
