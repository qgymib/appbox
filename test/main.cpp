#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <base64.hpp>
#include "probe/__init__.hpp"
#include "utils/Config.hpp"

int wmain(int argc, wchar_t* argv[])
{
    CLI::App app("AppBox unit tests");
    appbox::test::SetupTestConfig(app);
    appbox::test::ProbeInit(app);

    testing::InitGoogleTest(&argc, argv);
    CLI11_PARSE(app, argc, argv);

    return RUN_ALL_TESTS();
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
