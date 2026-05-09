#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <CLI/Encoding.hpp>
#include <base64.hpp>
#include "utils/Semaphore.hpp"
#include "RemoteServer.hpp"
#include "BuildCommandLine.hpp"
#include "Test.hpp"
#include "loader/Config.hpp"
#include "WString.hpp"

struct TestArgumentsPassthrough : testing::Test
{
};

static std::wstring GetExePath()
{
    std::wstring buf(MAX_PATH, L'\0');
    while (true)
    {
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len < buf.size())
        {
            buf.resize(len);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

TEST_F(TestArgumentsPassthrough, Hello)
{
    const uint32_t timeout_ms = 10 * 1000;
    const char*    method = "TestArgumentsPassthrough";
    const char*    pipe_path = R"(\\.\pipe\159c7c67-e4e2-41a4-bbcb-a68e788fd6c8)";
    auto           srv = appbox::RemoteServer::Create(pipe_path);

    appbox::Semaphore sem;
    srv->RegisterMethod(method, [&sem, srv](uint64_t id, const nlohmann::json& param) {
        auto msg = param.get<std::string>();
        EXPECT_EQ(msg, "Hello");

        srv->SendResponse(id, "World");
        sem.Release();
    });
    srv->Start();

    std::wstring cmd;
    {
        appbox::LoaderConfig config;
        auto                 exe_path = GetExePath();
        config.launch.executable = appbox::WideToUTF8(exe_path.c_str());

        nlohmann::json j_config = config;
        auto           base64_config = base64::to_base64(j_config.dump());

        /* clang-format off */
        cmd = appbox::BuildCommandLine(appbox::test::cmd_param.loader_path,
            {
                L"--X-AppBox-LogLevel", appbox::test::cmd_param.log_level,
                L"--X-AppBox-ConfigBase64", CLI::widen(base64_config),
                L"probe",
                L"--connect", CLI::widen(pipe_path),
                L"HelloWorld",
                L"--method", CLI::widen(method)
            }
        );
        /* clang-format on */
    }

    STARTUPINFOW startupInfo;
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    memset(&processInfo, 0, sizeof(processInfo));

    SPDLOG_TRACE(L"start process: {}", cmd);
    ASSERT_TRUE(CreateProcessW(appbox::test::cmd_param.loader_path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0,
                               nullptr, nullptr, &startupInfo, &processInfo));
    ASSERT_EQ(WaitForSingleObject(processInfo.hProcess, timeout_ms), WAIT_OBJECT_0);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    ASSERT_TRUE(sem.Acquire(timeout_ms));
}
