#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <spdlog/spdlog.h>
#include "utils/CommandLine.hpp"
#include "SetLogLevel.hpp"
#include "Test.hpp"

appbox::test::TestConfig appbox::test::config;

static void SetLogLevelFromString(const std::wstring& level)
{
    appbox::test::config.log_level = level;
    appbox::SetLogLevel(level);
}

static void SetConfigFromEnv()
{
    std::wstring val;
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_LOADER", val))
    {
        appbox::test::config.loader_path = val;
    }
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_LOG_LEVEL", val))
    {
        SetLogLevelFromString(val);
    }
    if (appbox::test::ReadEnvironmentVariable(L"APPBOX_TEST_NO_CLEANUP", val))
    {
        appbox::test::config.no_cleanup = _wcsicmp(val.c_str(), L"0") && _wcsicmp(val.c_str(), L"false") &&
                                          _wcsicmp(val.c_str(), L"no") && _wcsicmp(val.c_str(), L"off");
    }

    /* The environment is read first, so an option of the command line wins. */
    appbox::test::LoadTestTimeoutFromEnvironment(appbox::test::config.test_timeout);
}

int appbox::test::SetupTestConfig(CLI::App& app)
{
    SetConfigFromEnv();

    app.add_option("--loader", appbox::test::config.loader_path,
                   "Path to loader. Environment variable: APPBOX_TEST_LOADER.");
    app.add_option_function<std::wstring>("--log-level", SetLogLevelFromString,
                                          "Log level. Available levels: trace, debug, info, warn, err, critical, off. "
                                          "Default: info. Environment variable: APPBOX_TEST_LOG_LEVEL.");
    app.add_option("--no-cleanup", appbox::test::config.no_cleanup,
                   "Do not cleanup the test directory. Environment variable: APPBOX_TEST_NO_CLEANUP.");
    app.add_option("--test-timeout", appbox::test::config.test_timeout.test_timeout_seconds,
                   "Timeout of one test case, in seconds. 0 turns the watchdog off. Default: 300. "
                   "Environment variable: APPBOX_TEST_TIMEOUT.")
        ->check(CLI::NonNegativeNumber);
    app.add_option("--test-dump-dir", appbox::test::config.test_timeout.test_dump_dir,
                   "Directory the coredumps of a timed out test case are written to. Default: a `coredump` "
                   "directory below the test executable. Environment variable: APPBOX_TEST_DUMP_DIR.");

    return 0;
}
