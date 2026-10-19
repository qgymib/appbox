#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <CLI/CLI.hpp>
#include "probe/__init__.hpp"
#include "probe/RegChildProcess.hpp"

/**
 * @brief Entry point of the probe executable.
 *
 * The probe is the program a case runs inside the sandbox: the launcher starts
 * this executable with the sandbox module injected, the probe asks the test
 * process for its task over the named pipe of the case and reports the result
 * back. The executable is built for both bit sizes, so a case can run its
 * operations inside a 32 bit process as well.
 *
 * @param[in] argc Number of command line arguments.
 * @param[in] argv Command line arguments.
 * @return The result of the run.
 */
int wmain(int argc, wchar_t* argv[])
{
    CLI::App app("AppBox probe");
    appbox::test::ProbeInit(app);

    /*
     * The worker a sandboxed probe starts as a child process of its own. It is
     * a subcommand of this executable, so the sandbox injects the same module
     * into it.
     */
    appbox::test::RegChildProcessInit(app);

    CLI11_PARSE(app, argc, argv);
    return 0;
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
