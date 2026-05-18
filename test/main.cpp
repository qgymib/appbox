#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <base64.hpp>
#include "probe/__init__.hpp"
#include "SetLogLevel.hpp"
#include "Test.hpp"

static void SetLogLevel(const std::wstring& level)
{
    appbox::test::cmd_param.log_level = level;
    appbox::SetLogLevel(level);
}

static void SetLogLevelFromEnv()
{
    wchar_t log_level[256];
    DWORD   size = GetEnvironmentVariableW(L"APPBOX_LOG_LEVEL", log_level, sizeof(log_level) / sizeof(wchar_t));
    if (size > 0)
    {
        ::SetLogLevel(std::wstring(log_level, size));
    }
}

int wmain(int argc, wchar_t* argv[])
{
    SetLogLevelFromEnv();
    testing::InitGoogleTest(&argc, argv);

    CLI::App app("AppBox unit tests");
    app.add_option("--loader", appbox::test::cmd_param.loader_path);
    app.add_option_function<std::wstring>("--log-level", ::SetLogLevel);
    appbox::test::ProbeInit(app);

    SPDLOG_DEBUG("test suit start");
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

int main()
{
    CommandLine cmd;
    return wmain(cmd.wargc, cmd.wargv);
}

#endif
