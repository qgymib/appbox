#include "tracer/CdbSession.hpp"
#include "tracer/CdbOutputParser.hpp"
#include "tracer/Console.hpp"
#include "tracer/TraceReport.hpp"
#include "tracer/TracedModules.hpp"
#include "BuildCommandLine.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>

namespace appbox::tracer
{
namespace
{

/** Upper bound of the raw output which is kept for `--keep-raw`. */
constexpr std::size_t kMaxRawBytes = 16U * 1024U * 1024U;

/** Size of one read from the debugger output. */
constexpr std::size_t kReadBufferSize = 64U * 1024U;

/** Size of one write of a fed command batch. */
constexpr std::size_t kWriteChunkSize = 8U * 1024U;

/** Interval between two checks of the watchdogs. */
constexpr std::chrono::milliseconds kPollInterval{100};

/** Time the debugger gets to exit after its output ended. */
constexpr DWORD kExitWaitMilliseconds = 10000U;

/** Time the debugger gets to die after it was terminated. */
constexpr DWORD kKillWaitMilliseconds = 5000U;

/** Set when the user interrupts the run. */
std::atomic<bool> g_interrupted{false};

/**
 * @brief Console control handler which asks the running session to stop.
 *
 * @param[in] type Type of the control event.
 * @return Whether the event was handled.
 */
BOOL WINAPI ConsoleControlHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)
    {
        g_interrupted.store(true);
        return TRUE;
    }

    return FALSE;
}

/** RAII owner of a kernel handle. */
class HandleGuard
{
public:
    HandleGuard() = default;

    /**
     * @brief Take ownership of a handle.
     *
     * @param[in] handle Handle to own; may be null.
     */
    explicit HandleGuard(HANDLE handle) noexcept : handle_(handle) {}

    ~HandleGuard() { Close(); }

    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

    /**
     * @brief Take over the handle of another owner.
     *
     * @param[in,out] other Owner which gives up its handle.
     */
    HandleGuard(HandleGuard&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

    /**
     * @brief Take over the handle of another owner.
     *
     * @param[in,out] other Owner which gives up its handle.
     * @return This owner.
     */
    HandleGuard& operator=(HandleGuard&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }

        return *this;
    }

    /** @return The owned handle. */
    HANDLE Get() const noexcept { return handle_; }

    /** @return Address of the owned handle, for an out parameter. */
    HANDLE* Put() noexcept { return &handle_; }

    /** Close the owned handle. */
    void Close() noexcept
    {
        if (handle_ != nullptr)
        {
            ::CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

private:
    HANDLE handle_ = nullptr; ///< Owned handle.
};

/** RAII registration of the console control handler. */
class ConsoleHandlerGuard
{
public:
    ConsoleHandlerGuard()
    {
        g_interrupted.store(false);
        registered_ = ::SetConsoleCtrlHandler(ConsoleControlHandler, TRUE) != 0;
    }

    ~ConsoleHandlerGuard()
    {
        if (registered_)
        {
            ::SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
        }
    }

    ConsoleHandlerGuard(const ConsoleHandlerGuard&) = delete;
    ConsoleHandlerGuard& operator=(const ConsoleHandlerGuard&) = delete;

private:
    bool registered_ = false; ///< Whether the handler was installed.
};

/** Queue which the reader thread fills and the session loop consumes. */
class EventQueue
{
public:
    /**
     * @brief Append an event.
     *
     * @param[in] event Event to append.
     */
    void Push(const CdbEvent& event)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            events_.push_back(event);
        }

        condition_.notify_all();
    }

    /**
     * @brief Wait for events.
     *
     * @param[in] timeout How long to wait.
     * @param[out] events Events which arrived; appended to the vector.
     */
    void WaitFor(std::chrono::milliseconds timeout, std::vector<CdbEvent>& events)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, timeout, [this] { return !events_.empty() || closed_; });

        for (const auto& event : events_)
        {
            events.push_back(event);
        }

        events_.clear();
    }

    /** Mark the queue as complete; no further event arrives. */
    void Close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }

        condition_.notify_all();
    }

    /** @return Whether the producer finished. */
    bool Closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    mutable std::mutex mutex_;           ///< Guards the queue.
    std::condition_variable condition_;  ///< Signals new events.
    std::deque<CdbEvent> events_;        ///< Events which were not consumed yet.
    bool closed_ = false;                ///< Whether the producer finished.
};

/**
 * @brief Feeds command batches to the debugger without blocking the session.
 *
 * A batch can be larger than the buffer of the pipe, so a write may wait until
 * the debugger consumes it. The session loop must not wait with it: it has to
 * keep watching the debugger output and the watchdogs.
 */
class CommandWriter
{
public:
    /**
     * @brief Start the writer.
     *
     * @param[in] handle Write end of the standard input of the debugger.
     */
    explicit CommandWriter(HANDLE handle) : handle_(handle), thread_(&CommandWriter::Run, this) {}

    ~CommandWriter() { Stop(); }

    CommandWriter(const CommandWriter&) = delete;
    CommandWriter& operator=(const CommandWriter&) = delete;

    /**
     * @brief Queue a batch.
     *
     * @param[in] payload Text to write, including the line breaks.
     */
    void Post(std::string payload)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_)
            {
                return;
            }

            queue_.push_back(std::move(payload));
        }

        condition_.notify_all();
    }

    /** Stop the writer and wait until it returned. */
    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }

        condition_.notify_all();

        if (thread_.joinable())
        {
            thread_.join();
        }
    }

private:
    /** Write the queued batches until the writer is stopped. */
    void Run()
    {
        for (;;)
        {
            std::string payload;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
                if (queue_.empty())
                {
                    if (stopped_)
                    {
                        return;
                    }

                    continue;
                }

                payload = std::move(queue_.front());
                queue_.pop_front();
            }

            Write(payload);
        }
    }

    /**
     * @brief Write one batch in chunks.
     *
     * @param[in] payload Text to write.
     */
    void Write(const std::string& payload)
    {
        std::size_t offset = 0;
        while (offset < payload.size())
        {
            if (stopped_.load())
            {
                return;
            }

            /* std::min is not used here: the min macro of the Windows headers
             * would break the call. */
            const std::size_t remaining = payload.size() - offset;
            const std::size_t length = remaining < kWriteChunkSize ? remaining : kWriteChunkSize;
            DWORD written = 0;
            if (::WriteFile(handle_, payload.data() + offset, static_cast<DWORD>(length), &written,
                            nullptr) == 0 ||
                written == 0)
            {
                /* The debugger is gone or the pipe was cancelled. */
                return;
            }

            offset += written;
        }
    }

    HANDLE handle_ = nullptr;             ///< Standard input of the debugger.
    std::mutex mutex_;                    ///< Guards the queue.
    std::condition_variable condition_;   ///< Signals a queued batch.
    std::deque<std::string> queue_;       ///< Batches which were not written yet.
    std::atomic<bool> stopped_{false};    ///< Whether the writer has to stop.
    std::thread thread_;                  ///< Writer thread.
};

/**
 * @brief Create an anonymous pipe with explicit inheritance.
 *
 * Only the end which the debugger uses may be inherited: its standard input is
 * the read end of one pipe and its standard output is the write end of another
 * one. The ends which the tracer keeps must not be inherited, otherwise the
 * pipes stay open after the debugger died and the reader never sees the end of
 * the output.
 *
 * @param[out] read_end Read end of the pipe.
 * @param[out] write_end Write end of the pipe.
 * @param[in] inherit_read Whether the debugger inherits the read end.
 * @param[in] inherit_write Whether the debugger inherits the write end.
 * @return Whether the pipe was created.
 */
bool CreatePipeWithInheritance(HandleGuard& read_end, HandleGuard& write_end, bool inherit_read,
                               bool inherit_write)
{
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (::CreatePipe(&read, &write, &attributes, 0) == 0)
    {
        return false;
    }

    const DWORD read_flags = inherit_read ? HANDLE_FLAG_INHERIT : 0U;
    const DWORD write_flags = inherit_write ? HANDLE_FLAG_INHERIT : 0U;
    if (::SetHandleInformation(read, HANDLE_FLAG_INHERIT, read_flags) == 0 ||
        ::SetHandleInformation(write, HANDLE_FLAG_INHERIT, write_flags) == 0)
    {
        ::CloseHandle(read);
        ::CloseHandle(write);
        return false;
    }

    read_end = HandleGuard(read);
    write_end = HandleGuard(write);
    return true;
}

/** @return Milliseconds since an arbitrary point, for the watchdogs. */
long long NowMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/**
 * @brief Build the payload which arms one session.
 *
 * @param[in] plan Breakpoint plan.
 * @param[in] bases Module base addresses of the session.
 * @param[out] breakpoints Number of breakpoints in the payload.
 * @return The command text, including the continue command.
 */
std::string BuildSessionPayload(const std::vector<ArmGroup>& plan, const ModuleBases& bases,
                                std::size_t& breakpoints)
{
    const std::vector<std::string> lines = BuildArmLines(plan, bases);
    breakpoints = lines.size();

    std::string payload;
    for (const auto& line : lines)
    {
        payload += line;
        payload += '\n';
    }

    payload += "g\n";
    return payload;
}

} // namespace

TraceResult RunTraceSession(const TraceRequest& request)
{
    TraceResult result;
    ConsoleHandlerGuard console_handler;

    HandleGuard input_read;
    HandleGuard input_write;
    HandleGuard output_read;
    HandleGuard output_write;
    const bool input_pipe = CreatePipeWithInheritance(input_read, input_write, true, false);
    const bool output_pipe = CreatePipeWithInheritance(output_read, output_write, false, true);
    if (!input_pipe || !output_pipe)
    {
        result.status = RunStatus::Failed;
        result.message = L"the pipes of the debugger could not be created";
        return result;
    }

    std::vector<std::wstring> debugger_arguments{L"-G", L"-o", request.program.wstring()};
    debugger_arguments.insert(debugger_arguments.end(), request.program_args.begin(),
                              request.program_args.end());
    std::wstring command_line =
        appbox::BuildCommandLine(request.debugger.wstring(), debugger_arguments);
    const std::wstring application = request.debugger.wstring();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read.Get();
    startup.hStdOutput = output_write.Get();
    startup.hStdError = output_write.Get();

    PROCESS_INFORMATION process{};
    if (::CreateProcessW(application.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) == 0)
    {
        result.status = RunStatus::Failed;
        result.message = L"the debugger could not be started (error " +
                         std::to_wstring(static_cast<unsigned long>(::GetLastError())) + L")";
        return result;
    }

    HandleGuard process_handle(process.hProcess);
    HandleGuard thread_handle(process.hThread);

    /* The debugger owns its ends of the pipes from here on. */
    input_read.Close();
    output_write.Close();

    std::mutex raw_mutex;
    std::string raw_output;
    bool raw_truncated = false;
    std::atomic<long long> last_output{NowMilliseconds()};
    EventQueue queue;

    std::thread reader([&] {
        std::vector<char> buffer(kReadBufferSize);
        CdbOutputParser parser;
        for (;;)
        {
            DWORD length = 0;
            if (::ReadFile(output_read.Get(), buffer.data(), static_cast<DWORD>(buffer.size()),
                           &length, nullptr) == 0 ||
                length == 0)
            {
                break;
            }

            last_output.store(NowMilliseconds());

            const std::string_view chunk(buffer.data(), length);
            {
                std::lock_guard<std::mutex> lock(raw_mutex);
                const std::size_t room = raw_output.size() < kMaxRawBytes ? kMaxRawBytes - raw_output.size() : 0U;
                if (room == 0U)
                {
                    raw_truncated = true;
                }
                else
                {
                    raw_output.append(chunk.substr(0, room));
                }
            }

            std::vector<CdbEvent> events;
            parser.Feed(chunk, events);
            for (const auto& event : events)
            {
                queue.Push(event);
            }
        }

        queue.Close();
    });

    CommandWriter writer(input_write.Get());
    const auto started = std::chrono::steady_clock::now();

    std::size_t commands_fed = 0;
    std::size_t prompts_seen = 0;
    std::set<std::uint32_t> armed_sessions;
    ModuleBases bases;
    std::set<std::wstring> names;
    std::vector<std::size_t> calls_per_process;
    std::uint32_t current_session = 0;
    std::size_t reported = 0;

    for (;;)
    {
        std::vector<CdbEvent> events;
        queue.WaitFor(kPollInterval, events);

        for (const auto& event : events)
        {
            switch (event.kind)
            {
            case CdbEvent::Kind::ModuleLoad:
            {
                /* The newest module load lines belong to the newest process. */
                const std::wstring module = ModuleNameFromImagePath(event.image_path);
                if (!module.empty())
                {
                    bases[module] = event.image_base;
                }

                break;
            }
            case CdbEvent::Kind::Hit:
                for (const auto& name : event.names)
                {
                    names.insert(name);
                }

                /* Attribute the hit to the process which is being traced. */
                if (calls_per_process.size() <= current_session)
                {
                    calls_per_process.resize(static_cast<std::size_t>(current_session) + 1U, 0U);
                }

                ++calls_per_process[current_session];
                break;
            case CdbEvent::Kind::Prompt:
            {
                current_session = event.session;
                ++prompts_seen;
                if (prompts_seen <= commands_fed)
                {
                    /* The prompt belongs to a command which was fed before. */
                    break;
                }

                if (armed_sessions.find(event.session) != armed_sessions.end())
                {
                    ++result.unexpected_stops;
                    ++commands_fed;
                    writer.Post("g\n");
                    WriteStderr(L"AppBoxTracer: the debugger stopped unexpectedly, continuing\n");
                    break;
                }

                std::size_t breakpoints = 0;
                const std::string payload = BuildSessionPayload(request.plan, bases, breakpoints);
                commands_fed += breakpoints + 1U;
                writer.Post(payload);
                armed_sessions.insert(event.session);
                if (result.breakpoints == 0U)
                {
                    result.breakpoints = breakpoints;
                }

                if (breakpoints == 0U)
                {
                    WriteStderr(L"AppBoxTracer: no breakpoint could be armed: the module base "
                                L"addresses of the process are unknown\n");
                }
                else
                {
                    WriteStderr(L"AppBoxTracer: process " + std::to_wstring(armed_sessions.size()) +
                                L" armed with " + std::to_wstring(breakpoints) + L" breakpoints\n");
                }

                break;
            }
            }
        }

        if (names.size() >= reported + 100U)
        {
            reported = names.size();
            WriteStderr(L"AppBoxTracer: " + std::to_wstring(reported) + L" functions used so far\n");
        }

        if (g_interrupted.load())
        {
            result.status = RunStatus::Interrupted;
            result.message = L"interrupted by the user";
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - started > std::chrono::seconds(request.timeout_seconds))
        {
            result.status = RunStatus::TimedOut;
            result.message = L"the time limit of " + std::to_wstring(request.timeout_seconds) +
                             L" seconds was reached";
            break;
        }

        /* Commands which the debugger did not answer are a stuck debugger. */
        if (commands_fed > prompts_seen)
        {
            const auto silence = NowMilliseconds() - last_output.load();
            if (silence > static_cast<long long>(request.stall_timeout_seconds) * 1000LL)
            {
                result.status = RunStatus::Stalled;
                result.message = L"the debugger did not consume the fed commands within " +
                                 std::to_wstring(request.stall_timeout_seconds) + L" seconds";
                break;
            }
        }

        if (queue.Closed())
        {
            break;
        }
    }

    /* Unblock a writer which waits for a debugger that does not read any more. */
    if (result.status != RunStatus::Completed)
    {
        ::CancelIoEx(input_write.Get(), nullptr);
        ::TerminateProcess(process_handle.Get(), 1U);
    }

    writer.Stop();
    if (reader.joinable())
    {
        reader.join();
    }

    ::WaitForSingleObject(process_handle.Get(),
                          result.status == RunStatus::Completed ? kExitWaitMilliseconds
                                                                : kKillWaitMilliseconds);

    DWORD exit_code = 0;
    if (::GetExitCodeProcess(process_handle.Get(), &exit_code) != 0)
    {
        result.debugger_exit_code = static_cast<int>(exit_code);
    }

    if (result.status == RunStatus::Completed && armed_sessions.empty())
    {
        result.status = RunStatus::Failed;
        result.message = L"the debugger did not reach the first prompt";
    }
    else if (result.status == RunStatus::Completed && commands_fed > prompts_seen)
    {
        /* Commands which were never answered mean that the debugger did not
         * read them; a silent empty report would hide that. */
        result.status = RunStatus::Failed;
        result.message = names.empty() ? L"the debugger did not answer the fed commands"
                                       : L"the debugger exited before it answered the last commands";
    }

    result.names.assign(names.begin(), names.end());
    result.calls_per_process = calls_per_process;
    result.processes = armed_sessions.size();

    if (!request.keep_raw_path.empty())
    {
        std::string raw;
        {
            std::lock_guard<std::mutex> lock(raw_mutex);
            raw = raw_output;
        }

        std::wstring text = DecodeConsoleBytes(raw);
        if (raw_truncated)
        {
            text += L"\n[AppBoxTracer] the raw output was truncated at " +
                    std::to_wstring(kMaxRawBytes) + L" bytes\n";
        }

        const std::wstring error = WriteUtf8File(request.keep_raw_path, text);
        if (!error.empty())
        {
            WriteStderr(L"AppBoxTracer: " + error + L"\n");
        }
    }

    return result;
}

} // namespace appbox::tracer
