#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include "WString.hpp"
#include "CommandLine.hpp"
#include "Coredump.hpp"
#include "TestTimeout.hpp"

namespace
{

/** Directory name of the dumps below the test executable. */
constexpr const wchar_t* kDefaultDumpDirName = L"coredump";

/** Upper bound of a timeout which is accepted, in seconds. */
constexpr long kMaxTimeoutSeconds = 86400;

/**
 * @brief Write a line to the standard error stream.
 *
 * The message of a timed out test case has to reach the console of the test
 * run whatever the log level of the run is, so it is written directly.
 *
 * @param[in] text The line to write, in UTF-8.
 */
void ReportToStderr(const std::string& text)
{
    std::fputs(text.c_str(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

/**
 * @brief Read a timeout in seconds.
 * @param[in] text The text of the option.
 * @param[out] seconds The timeout.
 * @return true when the text is a timeout the run accepts.
 */
bool ParseTimeoutSeconds(const std::wstring& text, int& seconds)
{
    if (text.empty())
    {
        return false;
    }

    wchar_t*   end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    if (end == nullptr || *end != L'\0' || value < 0 || value > kMaxTimeoutSeconds)
    {
        return false;
    }

    seconds = static_cast<int>(value);
    return true;
}

/**
 * @brief Everything the watchdog thread and the test cases share.
 *
 * The state is allocated once and never freed: the watchdog outlives the tests
 * and the process is terminated from it when a test case times out.
 */
struct WatchdogState
{
    std::mutex                             mutex;     /* Guards every field below. */
    std::condition_variable                cv;        /* Wakes the watchdog. */
    std::thread                            thread;    /* The watchdog itself. */
    bool                                   armed = false; /* A test case is running. */
    bool                                   stop = false;  /* The run is over. */
    std::chrono::steady_clock::time_point  deadline;  /* Deadline of the running case. */
    std::string                            test_name; /* `Suite.Case` of the running case. */
    appbox::test::TestTimeoutConfig        config;    /* Copy of the configuration of the run. */
};

/** The watchdog state of the process, `nullptr` while the watchdog is not installed. */
WatchdogState* g_state = nullptr;

/**
 * @brief Arm the watchdog for one test case.
 * @param[in] test_name The name of the test case.
 */
void ArmWatchdog(const std::string& test_name)
{
    if (g_state == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_state->mutex);
    if (g_state->config.test_timeout_seconds <= 0)
    {
        /* The watchdog is turned off. */
        return;
    }

    g_state->test_name = test_name;
    g_state->deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(g_state->config.test_timeout_seconds);
    g_state->armed = true;
    g_state->cv.notify_all();
}

/**
 * @brief Disarm the watchdog after a test case.
 */
void DisarmWatchdog()
{
    if (g_state == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_state->mutex);
    g_state->armed = false;
    g_state->cv.notify_all();
}

/**
 * @brief Start the coredump writer and wait for it.
 *
 * The writer is a process of its own, because `MiniDumpWriteDump` suspends
 * every thread of the process it dumps and must not be called from a thread of
 * that process.
 *
 * @param[in] request The work the writer has to do.
 */
void RunCoredumpWriter(const appbox::test::CoredumpRequest& request)
{
    const std::wstring exe_path = appbox::test::GetOwnExecutablePath();
    std::wstring       command_line = appbox::test::BuildCoredumpWriterCommandLine(request);

    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    /* The writer reports the dumps on the streams of the test run, so the
     * handles of the test process have to be inherited. */
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
    startup_info.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));

    if (!::CreateProcessW(exe_path.c_str(), command_line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                          &startup_info, &process_info))
    {
        ReportToStderr(fmt::format("failed to start the coredump writer: {}", ::GetLastError()));
        return;
    }

    const DWORD wait = ::WaitForSingleObject(
        process_info.hProcess, static_cast<DWORD>(appbox::test::kCoredumpWriterTimeoutSeconds) * 1000);
    if (wait != WAIT_OBJECT_0)
    {
        ReportToStderr(fmt::format("the coredump writer did not finish within {} seconds",
                                   appbox::test::kCoredumpWriterTimeoutSeconds));
        ::TerminateProcess(process_info.hProcess, 1);
        ::WaitForSingleObject(process_info.hProcess, 5000);
    }

    ::CloseHandle(process_info.hThread);
    ::CloseHandle(process_info.hProcess);
}

/**
 * @brief Handle a test case which ran too long.
 *
 * The function never returns: it writes the coredumps, terminates the child
 * processes of the test and terminates the test process with the timeout exit
 * code, so the run of the tests ends at once instead of waiting for a test
 * which will not finish.
 *
 * @param[in] test_name The name of the test case.
 * @param[in] config The configuration of the run.
 */
[[noreturn]] void HandleTestTimeout(const std::string& test_name, const appbox::test::TestTimeoutConfig& config)
{
    ReportToStderr(
        fmt::format("test case {} timed out after {} seconds", test_name, config.test_timeout_seconds));

    appbox::test::CoredumpRequest request;
    request.pid = ::GetCurrentProcessId();
    request.dump_dir = appbox::test::ResolveTestDumpDir(config);
    request.tag = appbox::UTF8ToWide(test_name);

    RunCoredumpWriter(request);

    /* The child processes of the test (the loader and the probe of an
     * end-to-end case) must not outlive it. */
    appbox::test::TerminateProcessTree(::GetCurrentProcessId(), 0);

    ReportToStderr(fmt::format("the coredumps of {} are in {}", test_name,
                               appbox::WideToUTF8(request.dump_dir.wstring())));
    std::fflush(nullptr);

    /*
     * TerminateProcess instead of ExitProcess: a test which timed out may hold
     * the loader lock, and the exit handlers of the process would hang on it.
     */
    ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(appbox::test::kTestTimeoutExitCode));
    std::_Exit(appbox::test::kTestTimeoutExitCode);
}

/**
 * @brief The watchdog thread: it waits for the deadline of the running case.
 */
void WatchdogMain()
{
    for (;;)
    {
        std::unique_lock<std::mutex> lock(g_state->mutex);
        g_state->cv.wait(lock, [] { return g_state->stop || g_state->armed; });
        if (g_state->stop)
        {
            return;
        }

        const std::chrono::steady_clock::time_point deadline = g_state->deadline;

        /* The wait ends early when the run is over, when the test case ends or
         * when the test case moves its deadline back. */
        const bool woken = g_state->cv.wait_until(lock, deadline, [deadline] {
            return g_state->stop || !g_state->armed || g_state->deadline != deadline;
        });
        if (woken)
        {
            continue;
        }

        const std::string                      test_name = g_state->test_name;
        const appbox::test::TestTimeoutConfig  config = g_state->config;
        lock.unlock();

        HandleTestTimeout(test_name, config);
    }
}

/**
 * @brief The GoogleTest hook which arms and disarms the watchdog.
 */
class TestTimeoutListener : public testing::EmptyTestEventListener
{
public:
    /**
     * @brief Arm the watchdog with the name of the test case which starts.
     * @param[in] test_info The test case which starts.
     */
    void OnTestStart(const testing::TestInfo& test_info) override
    {
        const char* suite = test_info.test_suite_name();
        const char* name = test_info.name();

        std::string text = suite != nullptr ? suite : "";
        text += ".";
        text += (name != nullptr ? name : "");

        ArmWatchdog(text);
    }

    /**
     * @brief Disarm the watchdog after a test case.
     * @param[in] test_info The test case which ended.
     */
    void OnTestEnd(const testing::TestInfo& /*test_info*/) override
    {
        DisarmWatchdog();
    }
};

} // namespace

void appbox::test::LoadTestTimeoutFromEnvironment(TestTimeoutConfig& config)
{
    std::wstring value;
    if (ReadEnvironmentVariable(kTestTimeoutEnv, value))
    {
        int seconds = 0;
        if (ParseTimeoutSeconds(value, seconds))
        {
            config.test_timeout_seconds = seconds;
        }
        else
        {
            ReportToStderr(fmt::format("{} is not a timeout in seconds: {}", appbox::WideToUTF8(kTestTimeoutEnv),
                                       appbox::WideToUTF8(value)));
        }
    }

    if (ReadEnvironmentVariable(kTestDumpDirEnv, value) && !value.empty())
    {
        config.test_dump_dir = value;
    }
}

void appbox::test::LoadTestTimeoutFromCommandLine(TestTimeoutConfig& config)
{
    const std::wstring command_line = GetOwnCommandLine();
    std::wstring       value;

    if (FindCommandLineOption(command_line, kTestTimeoutOption, value))
    {
        int seconds = 0;
        if (ParseTimeoutSeconds(value, seconds))
        {
            config.test_timeout_seconds = seconds;
        }
        else
        {
            ReportToStderr(fmt::format("--{} is not a timeout in seconds: {}", appbox::WideToUTF8(kTestTimeoutOption),
                                       appbox::WideToUTF8(value)));
        }
    }

    if (FindCommandLineOption(command_line, kTestDumpDirOption, value) && !value.empty())
    {
        config.test_dump_dir = value;
    }
}

std::filesystem::path appbox::test::ResolveTestDumpDir(const TestTimeoutConfig& config)
{
    if (!config.test_dump_dir.empty())
    {
        return std::filesystem::path(config.test_dump_dir);
    }

    /* Default: a directory below the test executable, so the dumps of a run
     * land in the build tree of that run. */
    return std::filesystem::path(GetOwnExecutablePath()).parent_path() / kDefaultDumpDirName;
}

void appbox::test::InstallTestTimeoutHook(const TestTimeoutConfig& config)
{
    if (g_state != nullptr)
    {
        return;
    }

    g_state = new WatchdogState();
    g_state->config = config;

    if (config.test_timeout_seconds <= 0)
    {
        /* The watchdog is turned off: no listener and no thread. */
        return;
    }

    testing::UnitTest::GetInstance()->listeners().Append(new TestTimeoutListener());
    g_state->thread = std::thread(WatchdogMain);
}

void appbox::test::StopTestTimeoutHook()
{
    if (g_state == nullptr)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_state->mutex);
        g_state->stop = true;
        g_state->armed = false;
    }
    g_state->cv.notify_all();

    if (g_state->thread.joinable())
    {
        g_state->thread.join();
    }
}

void appbox::test::SetTestTimeout(int seconds)
{
    if (g_state == nullptr || seconds <= 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_state->mutex);
    if (!g_state->armed)
    {
        return;
    }

    /* The deadline is only ever moved back, so a case which asks for a shorter
     * budget than the one of the run keeps the budget of the run. */
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    if (deadline > g_state->deadline)
    {
        g_state->deadline = deadline;
        g_state->cv.notify_all();
    }
}
