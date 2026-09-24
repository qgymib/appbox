#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <base64.hpp>
#include "probe/__init__.hpp"
#include "utils/Coredump.hpp"
#include "utils/TestTimeout.hpp"
#include "Test.hpp"

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

    CLI::App app("AppBox unit tests");
    appbox::test::SetupTestConfig(app);
    appbox::test::ProbeInit(app);

    testing::InitGoogleTest(&argc, argv);
    CLI11_PARSE(app, argc, argv);

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
