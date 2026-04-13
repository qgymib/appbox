#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h> /* Required by detours.h */
#include <CLI/CLI.hpp>
#include <detours.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <chrono>
#include "WString.hpp"
#include "Defines.hpp"
#include "BuildCommandLine.hpp"
#include "SetLogLevel.hpp"
#include "GetExecutablePath.hpp"
#include "Loader.hpp"

struct LoaderParam
{
    std::wstring              sandbox_path; /* Sandbox path */
    std::wstring              exe_path;     /* Main executable path */
    std::vector<std::wstring> exe_args;     /* Main executable arguments */
};

int MainLoader(const LoaderParam& param)
{
#if defined(_WIN64)
    auto& sandbox_dll_path = appbox::loader->inject_data.sandbox64_dll_path;
#else
    auto& sandbox_dll_path = appbox::loader->inject_data.sandbox32_dll_path;
#endif

    const GUID guid = SANDBOX_GUID;

    STARTUPINFOW startupInfo;
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    memset(&processInfo, 0, sizeof(processInfo));

    auto cmdline = appbox::BuildCommandLine(param.exe_path, param.exe_args);
    SPDLOG_TRACE(L"cmd: {}", cmdline);

    if (!DetourCreateProcessWithDllExW(param.exe_path.c_str(), cmdline.data(), nullptr, nullptr,
                                       FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startupInfo,
                                       &processInfo, sandbox_dll_path.c_str(), nullptr))
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
    LoaderParam param;
    param.sandbox_path = std::filesystem::path(appbox::GetExecutableDir()) / L"sandbox";

    CLI::App app;
    app.prefix_command();
    app.add_option_function<std::wstring>("--log-level", appbox::SetLogLevel,
                                          "Set application log level");
    app.add_option(
        "--sandbox", param.sandbox_path,
        CLI::narrow(fmt::format(L"Path to sandbox directory. Default: {}", param.sandbox_path)));
    CLI11_PARSE(app, argc, argv);

    /* canonical requires the path actually exists. */
    std::filesystem::create_directories(param.sandbox_path);
    param.sandbox_path = std::filesystem::canonical(param.sandbox_path);

    /* Parse executable and arguments. */
    {
        auto remaining = app.remaining();
        param.exe_path = CLI::widen(remaining[0]);

        remaining.erase(remaining.begin());
        for (auto& arg : remaining)
        {
            param.exe_args.push_back(CLI::widen(arg));
        }
    }

    appbox::loader = appbox::Loader::Create(param.sandbox_path);
    return MainLoader(param);
}
