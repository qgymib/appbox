#include <CLI/CLI.hpp>
#include <detours.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include "WinAPI.hpp"
#include "WString.hpp"
#include "Defines.hpp"
#include "InjectData.hpp"
#include "CRC32.hpp"

struct LoaderCtx
{
    std::string        executable_path;
    appbox::InjectData inject_data; /* Inject data information */
};

static LoaderCtx s_ctx;

static void GenerateInjectDataPipePath()
{
    std::stringstream ss_crc32;
    ss_crc32 << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
             << appbox::CRC32::Update(0, s_ctx.executable_path.c_str(),
                                      s_ctx.executable_path.size());

    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    s_ctx.inject_data.pipe_path =
        fmt::format(R"(\\.\pipe\appbox-{}-{})", timestamp, ss_crc32.str());
}

void MainLoader()
{
#if defined(_WIN64)
    auto& sandbox_dll_path = s_ctx.inject_data.sandbox64_path;
#else
    auto& sandbox_dll_path = s_ctx.inject_data.sandbox32_path;
#endif

    GenerateInjectDataPipePath();

    const GUID guid = SANDBOX_GUID;

    STARTUPINFOW startupInfo;
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    memset(&processInfo, 0, sizeof(processInfo));

    auto wExePath = appbox::UTF8ToWide(s_ctx.executable_path.c_str());
    if (!DetourCreateProcessWithDllExW(wExePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                                       CREATE_SUSPENDED, nullptr, nullptr, &startupInfo,
                                       &processInfo, sandbox_dll_path.c_str(), nullptr))
    {
        SPDLOG_ERROR("DetourCreateProcessWithDllExW(): failed");
        exit(EXIT_FAILURE);
    }

    std::string inject_data = nlohmann::json(s_ctx.inject_data).dump();
    if (!DetourCopyPayloadToProcess(processInfo.hProcess, guid, inject_data.c_str(),
                                    (DWORD)inject_data.size()))
    {
        SPDLOG_ERROR("DetourCopyPayloadToProcess(): failed");
        exit(EXIT_FAILURE);
    }

    ResumeThread(processInfo.hThread);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
}

int wmain(int argc, wchar_t* argv[])
{
    CLI::App app;
    app.add_option("--sandbox32", s_ctx.inject_data.sandbox32_path, "Path to 32-bit sandbox dll")
        ->required();
    app.add_option("--sandbox64", s_ctx.inject_data.sandbox64_path, "Path to 64-bit sandbox dll")
        ->required();
    app.add_option("executable", s_ctx.executable_path, "Path to executable")->required();
    CLI11_PARSE(app, argc, argv);

    MainLoader();
    return 0;
}
