#include <gtest/gtest.h>
#include "probe/__init__.hpp"
#include "utils/CommonFixture.hpp"
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
