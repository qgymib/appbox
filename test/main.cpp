#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <base64.hpp>
#include "probe/__init__.hpp"
#include "utils/Coredump.hpp"
#include "utils/NameResolutionProbe.hpp"
#include "utils/TestLog.hpp"
#include "utils/TestTimeout.hpp"
#include "Test.hpp"

/**
 * @brief Entry point of the AppBox test executable.
 *
 * The executable runs both sides of the test suite of the project: the
 * in-process unit tests below `test/unit` and the end-to-end cases below
 * `test/e2e`. The `--mode` option selects the side; the end-to-end cases
 * start the real launcher, which injects the sandbox DLL and starts this
 * executable again as the probe process of the case.
 *
 * @param[in] argc Number of command line arguments.
 * @param[in] argv Command line arguments.
 * @return The result of the test run.
 */
int wmain(int argc, wchar_t* argv[])
{
    /*
     * The coredump writer is this executable itself. It has to return before
     * CLI11 and GoogleTest look at the command line, which holds the options
     * of the writer instead of the options of a test run.
     */
    if (appbox::test::RunCoredumpWriterIfRequested())
    {
        return 0;
    }

    /*
     * The name resolution probe is the target of the integration test of the
     * tracer; like the coredump writer it returns before GoogleTest looks at
     * the command line.
     */
    if (appbox::test::RunNameResolutionProbeIfRequested())
    {
        return 0;
    }

    CLI::App app("AppBox tests");
    appbox::test::SetupTestConfig(app);
    appbox::test::ProbeInit(app);

    testing::InitGoogleTest(&argc, argv);
    CLI11_PARSE(app, argc, argv);

    /*
     * The mode is applied after GoogleTest read its own flags, so the range of
     * the mode is combined with the filter of the command line: the filter only
     * narrows the range of the mode.
     */
    if (appbox::test::ApplyTestModeFilter(appbox::test::config.mode) != 0)
    {
        return 1;
    }

    /*
     * The program log of the test executable is captured from here on: it is
     * written out only when a case fails, so the run prints the GoogleTest
     * lines of a passing case and nothing else.
     */
    appbox::test::InstallTestLog();
    appbox::test::InstallTestTimeoutHook(appbox::test::config.test_timeout);

    const int result = RUN_ALL_TESTS();
    appbox::test::StopTestTimeoutHook();
    return result;
}

#if defined(__MINGW32__)

struct CommandLine
{
    CommandLine()
    {
        wargc = 0;
        wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    }

    ~CommandLine()
    {
        LocalFree(wargv);
    }

    int     wargc;
    LPWSTR* wargv;
};

/**
 * @brief Program entry point.
 * MinGW doesn't support wmain() entry point, so we need to use main() instead.
 */
int main()
{
    CommandLine cmd;
    return wmain(cmd.wargc, cmd.wargv);
}

#endif
