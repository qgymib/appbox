#ifndef APPBOX_TEST_UTILS_TESTTIMEOUT_HPP
#define APPBOX_TEST_UTILS_TESTTIMEOUT_HPP

#include <filesystem>
#include <string>

namespace appbox::test
{

/** Timeout of one test case, in seconds, when the run names no other one. */
constexpr int kDefaultTestTimeoutSeconds = 300;

/**
 * Exit code of a test run which the watchdog stopped because a test case timed
 * out. The value follows the convention of the `timeout` utility of GNU, so a
 * reader of the log of a test run recognises the reason without a lookup.
 */
constexpr int kTestTimeoutExitCode = 124;

/** Seconds the watchdog waits for the coredump writer before it gives up. */
constexpr int kCoredumpWriterTimeoutSeconds = 120;

/** Option which overrides the timeout of one test case, in seconds. */
constexpr const wchar_t* kTestTimeoutOption = L"test-timeout";
/** Option which names the directory of the coredumps. */
constexpr const wchar_t* kTestDumpDirOption = L"test-dump-dir";
/** Environment variable of the timeout of one test case. */
constexpr const wchar_t* kTestTimeoutEnv = L"APPBOX_TEST_TIMEOUT";
/** Environment variable of the directory of the coredumps. */
constexpr const wchar_t* kTestDumpDirEnv = L"APPBOX_TEST_DUMP_DIR";

/**
 * @brief The timeout configuration of a test run.
 */
struct TestTimeoutConfig
{
    int          test_timeout_seconds = kDefaultTestTimeoutSeconds; /* Timeout of one test case, 0 disables it. */
    std::wstring test_dump_dir;                                     /* Directory of the coredumps, empty for the default. */
};

/**
 * @brief Take the timeout options out of the environment of the process.
 * @param[in,out] config The configuration to update.
 */
void LoadTestTimeoutFromEnvironment(TestTimeoutConfig& config);

/**
 * @brief Take the timeout options out of the command line of the process.
 *
 * The wide command line of the process is read, so the helper works for a test
 * executable which parses its arguments with CLI11 as well as for one which
 * hands them to GoogleTest.
 *
 * @param[in,out] config The configuration to update.
 */
void LoadTestTimeoutFromCommandLine(TestTimeoutConfig& config);

/**
 * @brief Resolve the directory the coredumps of a run are written to.
 * @param[in] config The configuration of the run.
 * @return The directory of the configuration, or a `coredump` directory below
 *         the test executable when the configuration names none.
 */
std::filesystem::path ResolveTestDumpDir(const TestTimeoutConfig& config);

/**
 * @brief Install the watchdog which stops a test case that runs too long.
 *
 * The watchdog listens to the start and to the end of every test case of the
 * GoogleTest run. A test case which runs longer than the configured timeout
 * makes the watchdog write a coredump of the test process and of its child
 * processes, terminate those child processes and terminate the test process
 * with the exit code `kTestTimeoutExitCode`.
 *
 * A configuration with a timeout of 0 installs nothing, which turns the
 * watchdog off.
 *
 * @param[in] config The configuration of the run; it is copied, so the caller
 *                   may drop it afterwards.
 */
void InstallTestTimeoutHook(const TestTimeoutConfig& config);

/**
 * @brief Stop the watchdog of the run.
 *
 * The function has to be called after the tests ran, so the thread of the
 * watchdog is joined before the process ends. It is a no-op when no watchdog
 * was installed.
 */
void StopTestTimeoutHook();

/**
 * @brief Give the running test case a longer timeout.
 *
 * The deadline of the running test case is moved to `seconds` from now when
 * that is later than the deadline it already has, so a case whose own budget
 * is longer than the budget of the run is not stopped in the middle of its
 * work. The call is ignored outside of a test case and when the watchdog is
 * turned off.
 *
 * @param[in] seconds The budget of the test case, in seconds.
 */
void SetTestTimeout(int seconds);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_TESTTIMEOUT_HPP
