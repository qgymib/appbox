#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <random>
#include "RemoteClient.hpp"
#include "RemoteServer.hpp"

struct TestPipeServer : testing::Test
{
    TestPipeServer()
    {
        std::random_device rd;
        std::mt19937_64    gen(rd());
        pipe_path = R"(\\.\pipe\test_pipe_)" + std::to_string(gen());
    }

    std::string pipe_path;
};

TEST_F(TestPipeServer, Client)
{
    auto srv = appbox::RemoteServer::Create(pipe_path);

    srv->RegisterMethod("test", [srv](uint64_t id, const nlohmann::json& param) {
        std::string msg = param.get<std::string>();
        EXPECT_EQ(msg, "hello");

        srv->SendResponse(id, "world");
    });

    srv->Start();

    auto client = appbox::RemoteClient::Create(pipe_path);
    ASSERT_TRUE(client->Start());

    auto rsp = client->Call("test", "hello").get();
    ASSERT_TRUE(rsp.has_value());
    ASSERT_EQ(rsp.value().get<std::string>(), "world");
}
