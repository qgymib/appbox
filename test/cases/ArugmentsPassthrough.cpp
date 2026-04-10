#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include "RemoteServer.hpp"
#include "BuildCommandLine.hpp"
#include "Test.hpp"
#include "CLI/Encoding.hpp"

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
    const char* method = "TestArgumentsPassthrough";
    const char* pipe_path = R"(\\.\pipe\159c7c67-e4e2-41a4-bbcb-a68e788fd6c8)";
    auto        srv = appbox::RemoteServer::Create(pipe_path);

    bool  msg_received = false;
    bool* p_msg_received = &msg_received;

    srv->RegisterMethod(method, [srv, p_msg_received](uint64_t id, const nlohmann::json& param) {
        SPDLOG_TRACE("message received");

        auto msg = param.get<std::string>();
        EXPECT_EQ(msg, "Hello");

        *p_msg_received = true;
        srv->SendResponse(id, "World");
    });
    srv->Start();

    auto cmd = appbox::BuildCommandLine(
        appbox::test::cmd_param.loader_path,
        { L"--log-level", appbox::test::cmd_param.log_level, GetExePath(), L"probe", L"--connect",
          CLI::widen(pipe_path), L"HelloWorld", L"--method", CLI::widen(method) });

    STARTUPINFOW startupInfo;
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    memset(&processInfo, 0, sizeof(processInfo));

    SPDLOG_TRACE(L"start process: {}", cmd);
    ASSERT_TRUE(CreateProcessW(appbox::test::cmd_param.loader_path.c_str(), cmd.data(), nullptr,
                               nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo));
    WaitForSingleObject(processInfo.hProcess, INFINITE);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    ASSERT_TRUE(msg_received);
}
