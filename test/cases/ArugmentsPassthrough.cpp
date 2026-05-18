#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <CLI/Encoding.hpp>
#include <base64.hpp>
#include "probe/__init__.hpp"
#include "utils/Semaphore.hpp"
#include "RemoteServer.hpp"
#include "BuildCommandLine.hpp"
#include "Test.hpp"
#include "loader/Config.hpp"
#include "WString.hpp"

struct TestArgumentsPassthrough : testing::Test
{
};

nlohmann::json ProbeHelloWorld(const nlohmann::json& data)
{
    std::string v = data.get<std::string>();
    return v + "World";
}
static appbox::test::Probe probe("HelloWorld", ProbeHelloWorld);

TEST_F(TestArgumentsPassthrough, Hello)
{
    auto ret = probe.Call("Hello").get<std::string>();
    ASSERT_EQ(ret, "HelloWorld");
}
