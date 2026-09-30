#ifndef APPBOX_TEST_HPP
#define APPBOX_TEST_HPP

#include <string>
#include <CLI/CLI.hpp>
#include "utils/TestTimeout.hpp"

namespace appbox::test
{

/**
 * @brief The prefix of the suite name of a unit test.
 *
 * A unit test runs in the process of the test executable and is defined below
 * `test/unit`, so its suite name starts with this prefix. The prefix is the
 * single source of the mode of a suite: the range of a `--mode=unit` run is the
 * set of the suites which carry it.
 */
inline constexpr const char* kUnitSuitePrefix = "Unit_";

/**
 * @brief The prefix of the suite name of an end-to-end case.
 *
 * An end-to-end case starts the real launcher and is defined below `test/e2e`, so
 * its suite name starts with this prefix. `ApplyTestModeFilter` refuses a suite
 * whose prefix does not match the directory of its file, so a case which was
 * put in the wrong directory or which was spelled without its prefix stops the
 * run instead of being skipped silently.
 */
inline constexpr const char* kEndToEndSuitePrefix = "E2E_";

/** @brief The part of the test suites a run executes. */
enum class TestMode
{
    All,     /*!< Every suite. */
    Unit,    /*!< The in-process unit test suites only. */
    EndToEnd /*!< The end-to-end cases only. */
};

struct TestConfig
{
    std::wstring      launcher_path;        /* Path to launcher */
    std::wstring      sandbox32_path;       /* Path to the 32 bit sandbox injection module */
    std::wstring      sandbox64_path;       /* Path to the 64 bit sandbox injection module */
    std::wstring      packer_path;          /* Path to the packer */
    std::wstring      log_level = L"info";  /* Log level */
    bool              no_cleanup = false;   /* Do not cleanup the test directory */
    TestMode          mode = TestMode::All; /* Part of the suites the run executes */
    TestTimeoutConfig test_timeout;         /* Timeout of a test case and the coredumps of a timeout */
};

/**
 * @brief The configuration of the test.
 */
extern TestConfig config;

/**
 * @brief Get the name of a test mode as it is spelled on the command line.
 * @param[in] mode The test mode.
 * @return The name of the mode.
 */
const char* TestModeName(TestMode mode);

/**
 * @brief Parse the name of a test mode.
 * @param[in] text The name, for example `unit`.
 * @param[out] mode The parsed mode, unchanged when the name is unknown.
 * @return true when the name is a known mode, false otherwise.
 */
bool ParseTestMode(const std::wstring& text, TestMode& mode);

/**
 * @brief Setup the configuration of the test.
 * @param[in] app The CLI app.
 * @return 0 on success, otherwise a non-zero value.
 */
int SetupTestConfig(CLI::App& app);

/**
 * @brief Restrict the run to the suites of a mode.
 *
 * The mode is the range of a run and the GoogleTest filter of the command line
 * only narrows that range: the suites of the other mode are appended to the
 * negative part of the filter, which GoogleTest applies with precedence over
 * the positive part. A run of a mode therefore never leaves the mode, whatever
 * the filter names, while a filter which names the mode keeps its usual meaning
 * (`--mode=e2e --gtest_filter=E2E_Reg.Full_*` runs a single case of the mode).
 *
 * The function also checks that every registered suite carries the prefix of
 * the directory its file is defined in: a suite below `test/unit` has to start
 * with `kUnitSuitePrefix` and a suite below `test/e2e` with
 * `kEndToEndSuitePrefix`. A run which violates that rule stops before the first
 * test case, so a suite which was put in the wrong directory or which was
 * spelled without its prefix is reported instead of being skipped silently.
 *
 * @param[in] mode The mode of the run.
 * @return 0 on success, otherwise a non-zero value; a failure is reported on
 *         the standard error stream and no test case is run.
 */
int ApplyTestModeFilter(TestMode mode);

} // namespace appbox::test

#endif // APPBOX_TEST_HPP
