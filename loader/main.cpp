#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h> /* Required by detours.h */
#include <CLI/CLI.hpp>
#include <detours.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include "WString.hpp"
#include "Defines.hpp"
#include "BuildCommandLine.hpp"
#include "SetLogLevel.hpp"
#include "Loader.hpp"

int MainLoader()
{
#if defined(_WIN64)
    auto& sandbox_dll_path = appbox::loader->inject_data.sandbox64_path;
#else
    auto& sandbox_dll_path = appbox::loader->inject_data.sandbox32_path;
#endif

    const GUID guid = SANDBOX_GUID;

    STARTUPINFOW startupInfo;
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    memset(&processInfo, 0, sizeof(processInfo));

    auto cmdline = appbox::BuildCommandLine(appbox::loader->exe_path, appbox::loader->exe_args);
    SPDLOG_TRACE(L"cmd: {}", cmdline);

    if (!DetourCreateProcessWithDllExW(appbox::loader->exe_path.c_str(), cmdline.data(), nullptr,
                                       nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr,
                                       &startupInfo, &processInfo, sandbox_dll_path.c_str(),
                                       nullptr))
    {
        SPDLOG_ERROR("DetourCreateProcessWithDllExW(): failed");
        exit(EXIT_FAILURE);
    }

    std::string inject_data = nlohmann::json(appbox::loader->inject_data).dump();
    if (!DetourCopyPayloadToProcess(processInfo.hProcess, guid, inject_data.c_str(),
                                    static_cast<DWORD>(inject_data.size())))
    {
        SPDLOG_ERROR("DetourCopyPayloadToProcess(): failed");
        exit(EXIT_FAILURE);
    }

    /* Start and wait for process exit. */
    DWORD exit_code = 0;
    {
        ResumeThread(processInfo.hThread);
        CloseHandle(processInfo.hThread);
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        GetExitCodeProcess(processInfo.hProcess, &exit_code);
        CloseHandle(processInfo.hProcess);
    }

    return static_cast<int>(exit_code);
}

int wmain(int argc, wchar_t* argv[])
{
    CLI::App app;
    app.prefix_command();
    app.add_option_function<std::wstring>("--log-level", appbox::SetLogLevel,
                                          "Set application log level");

    appbox::InitLoader();
    atexit(appbox::ExitLoader);

    CLI11_PARSE(app, argc, argv);

    /* Parse executable and arguments. */
    {
        auto remaining = app.remaining();
        appbox::loader->exe_path = CLI::widen(remaining[0]);

        remaining.erase(remaining.begin());
        for (auto& arg : remaining)
        {
            appbox::loader->exe_args.push_back(CLI::widen(arg));
        }
    }

    return MainLoader();
}
