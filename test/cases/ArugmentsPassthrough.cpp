#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include "probe/__init__.hpp"
#include "loader/Config.hpp"
#include "utils/CommonFixture.hpp"
#include "Test.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture TestArgumentsPassthrough;

static nlohmann::json ProbeHelloWorld(const nlohmann::json& data)
{
    std::string v = data.get<std::string>();
    return v + "World";
}
static appbox::test::Probe probe("HelloWorld", ProbeHelloWorld);

TEST_F(TestArgumentsPassthrough, Hello)
{
    auto ret = probe.Call("Hello", GetCWDString(), {}).get<std::string>();
    ASSERT_EQ(ret, "HelloWorld");
}
