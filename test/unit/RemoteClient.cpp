/*
 * The RPC headers pull in winsock2.h through asio, they have to come before
 * <windows.h> which would otherwise include the winsock.h version 1 header.
 */
#include "RemoteClient.hpp"
#include "RemoteServer.hpp"
#include <gtest/gtest.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

namespace
{

/** Method of a server which echoes its parameter. */
constexpr const char* kEchoMethod = "appbox_unit_echo";

/** Method of a server which answers after a delay. */
constexpr const char* kSlowMethod = "appbox_unit_slow";

/**
 * @brief Message of the strong-typed call of a case.
 */
struct DeferredMessage
{
    static constexpr const char* Method = "appbox_unit_deferred";

    /**
     * @brief Request of the message.
     */
    struct Req
    {
        std::string value; /* Value of the request */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, value)
    };

    /**
     * @brief Response of the message.
     */
    struct Rsp
    {
        std::string value; /* Value of the response */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, value)
    };
};

/**
 * @brief Number of handles owned by the test process.
 * @return The handle count, zero when it cannot be queried.
 */
DWORD CountHandles()
{
    DWORD count = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &count))
    {
        return 0;
    }

    return count;
}

/**
 * @brief Build a unique temporary file path.
 * @return The path of a file which does not exist yet.
 */
std::string MakeTempFilePath()
{
    char        temp_dir[MAX_PATH] = {};
    const DWORD length = GetTempPathA(MAX_PATH, temp_dir);
    EXPECT_GT(length, 0u);

    std::random_device rd;
    std::mt19937_64    gen(rd());

    return std::string(temp_dir) + "appbox_unit_" + std::to_string(gen()) + ".tmp";
}

/**
 * @brief Build a unique named pipe path.
 * @return The pipe path.
 */
std::string MakePipePath()
{
    std::random_device rd;
    std::mt19937_64    gen(rd());

    return R"(\\.\pipe\appbox_unit_)" + std::to_string(gen());
}

/**
 * @brief Create a regular file which is not a named pipe.
 * @param[in] path Path of the file.
 */
void CreateRegularFile(const std::string& path)
{
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs << "this is not a named pipe";
}

} // namespace

/**
 * @brief Opening the client end of a path which is not a named pipe fails, and
 *        the handle opened for it is closed again.
 */
TEST(Unit_RemoteClient, StartOnRegularFileFailsWithoutLeakingHandle)
{
    const std::string file_path = MakeTempFilePath();
    CreateRegularFile(file_path);

    const DWORD before = CountHandles();

    {
        auto client = appbox::RemoteClient::Create(file_path);
        EXPECT_FALSE(client->Start());
    }

    const DWORD after = CountHandles();
    EXPECT_LE(after, before + 1); /* Allow a single handle of noise. */

    std::filesystem::remove(file_path);
}

/**
 * @brief Repeated failing connections do not accumulate handles.
 */
TEST(Unit_RemoteClient, RepeatedFailedStartDoesNotLeakHandles)
{
    const std::string file_path = MakeTempFilePath();
    CreateRegularFile(file_path);

    /* Warm up, the first attempt may create lazily initialized resources. */
    {
        auto client = appbox::RemoteClient::Create(file_path);
        EXPECT_FALSE(client->Start());
    }

    const DWORD before = CountHandles();

    for (int i = 0; i < 10; ++i)
    {
        auto client = appbox::RemoteClient::Create(file_path);
        EXPECT_FALSE(client->Start());
    }

    const DWORD after = CountHandles();
    EXPECT_LE(after, before + 1);

    std::filesystem::remove(file_path);
}

/**
 * @brief A pipe which does not exist makes the start fail.
 */
TEST(Unit_RemoteClient, StartOnMissingPipeFails)
{
    auto client = appbox::RemoteClient::Create(MakePipePath());

    /* The budget of the attempts is short, no server will ever appear. */
    EXPECT_FALSE(client->Start(200));
}

/**
 * @brief An error response of the server is delivered to the caller instead of
 *        being dropped or throwing.
 */
TEST(Unit_RemoteClient, CallUnknownMethodReturnsError)
{
    const std::string pipe_path = MakePipePath();

    auto server = appbox::RemoteServer::Create(pipe_path);
    server->Start();

    auto client = appbox::RemoteClient::Create(pipe_path);
    ASSERT_TRUE(client->Start());

    auto rsp = client->Call("appbox_unit_missing_method", nlohmann::json::object()).get();

    ASSERT_FALSE(rsp.has_value());
    EXPECT_EQ(rsp.error().code, -32601);
}

/**
 * @brief A regular request and response round trip still works.
 */
TEST(Unit_RemoteClient, CallRegisteredMethod)
{
    const std::string pipe_path = MakePipePath();

    auto server = appbox::RemoteServer::Create(pipe_path);
    server->RegisterMethod("appbox_unit_echo",
                           [server](uint64_t id, const nlohmann::json& param) { server->SendResponse(id, param); });
    server->Start();

    auto client = appbox::RemoteClient::Create(pipe_path);
    ASSERT_TRUE(client->Start());

    auto rsp = client->Call("appbox_unit_echo", "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");
}

/**
 * @brief A response which does not arrive in the budget of the call is reported
 *        as a timeout, and the server keeps serving afterwards.
 */
TEST(Unit_RemoteClient, CallTimesOut)
{
    const std::string pipe_path = MakePipePath();

    auto                  server = appbox::RemoteServer::Create(pipe_path);
    appbox::RemoteServer* raw = server.get();

    raw->RegisterMethod(kSlowMethod, [raw](uint64_t id, const nlohmann::json&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        raw->SendResponse(id, "late");
    });
    raw->RegisterMethod(kEchoMethod, [raw](uint64_t id, const nlohmann::json& param) { raw->SendResponse(id, param); });

    ASSERT_TRUE(raw->Start());

    auto client = appbox::RemoteClient::Create(pipe_path);
    ASSERT_TRUE(client->Start(5000));

    const auto started = std::chrono::steady_clock::now();
    const auto rsp = client->Call(kSlowMethod, nlohmann::json(), 100);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

    ASSERT_FALSE(rsp.has_value());
    EXPECT_EQ(rsp.error().code, static_cast<int>(ERROR_TIMEOUT));
    EXPECT_LT(elapsed.count(), 1000); /* The call does not wait for the answer. */

    /* The server keeps serving after the request of the client was dropped. */
    const auto echoed = client->Call(kEchoMethod, "hello", 5000);
    ASSERT_TRUE(echoed.has_value());
    EXPECT_EQ(echoed.value().get<std::string>(), "hello");

    raw->Stop();
}

/**
 * @brief The errors of a pipe which is not there yet are retried, so a client
 *        may start before its server does.
 */
TEST(Unit_RemoteClient, RetriesOnBrokenPipe)
{
    EXPECT_TRUE(appbox::detail::IsRetryableRemoteClientConnectError(ERROR_FILE_NOT_FOUND));
    EXPECT_TRUE(appbox::detail::IsRetryableRemoteClientConnectError(ERROR_PIPE_BUSY));
    EXPECT_TRUE(appbox::detail::IsRetryableRemoteClientConnectError(ERROR_PIPE_NOT_CONNECTED));
    EXPECT_TRUE(appbox::detail::IsRetryableRemoteClientConnectError(ERROR_BROKEN_PIPE));

    EXPECT_FALSE(appbox::detail::IsRetryableRemoteClientConnectError(ERROR_ACCESS_DENIED));
    EXPECT_FALSE(appbox::detail::IsRetryableRemoteClientConnectError(ERROR_INVALID_NAME));

    const std::string pipe_path = MakePipePath();

    /* The server appears while the client is already retrying the connection. */
    appbox::RemoteServer::Ptr server;
    std::thread               late_server([&server, &pipe_path]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        server = appbox::RemoteServer::Create(pipe_path);
        server->RegisterMethod(kEchoMethod, [raw = server.get()](uint64_t id, const nlohmann::json& param) {
            raw->SendResponse(id, param);
        });
        server->Start();
    });

    auto       client = appbox::RemoteClient::Create(pipe_path);
    const bool connected = client->Start(5000);

    late_server.join();

    ASSERT_TRUE(connected);
    ASSERT_NE(server, nullptr);

    const auto rsp = client->Call(kEchoMethod, "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    server->Stop();
}

/**
 * @brief The client reports the process of the server it is connected to.
 */
TEST(Unit_RemoteClient, ServerProcessIdIsAvailable)
{
    const std::string pipe_path = MakePipePath();

    auto server = appbox::RemoteServer::Create(pipe_path);
    ASSERT_TRUE(server->Start());

    auto client = appbox::RemoteClient::Create(pipe_path);
    ASSERT_TRUE(client->Start(5000));

    /* The server of the case runs in the process of the test. */
    EXPECT_EQ(client->ServerProcessId(), ::GetCurrentProcessId());

    server->Stop();
}

/**
 * @brief The strong-typed call cannot enforce a timeout: its deferred wrapper
 *        makes wait_for() return without running the task, so a caller which
 *        needs a budget has to use the blocking call of the transport.
 */
TEST(Unit_RemoteClient, DeferredCallCannotBeUsedForTimeout)
{
    const std::string pipe_path = MakePipePath();

    auto                  server = appbox::RemoteServer::Create(pipe_path);
    appbox::RemoteServer* raw = server.get();

    raw->RegisterMethod(DeferredMessage::Method, [raw](uint64_t id, const nlohmann::json&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        raw->SendResponse(id, DeferredMessage::Rsp{ "late" });
    });

    ASSERT_TRUE(raw->Start());

    auto client = appbox::RemoteClient::Create(pipe_path);
    ASSERT_TRUE(client->Start(5000));

    DeferredMessage::Req req;
    req.value = "hello";

    auto future = client->Call<DeferredMessage>(req);

    /*
     * The wrapper of the strong-typed call is a deferred task, so wait_for()
     * reports the state of the task without running it: the caller cannot tell
     * a pending response from a missing one.
     */
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(10)), std::future_status::deferred);

    /* The blocking call of the transport reports the timeout instead. */
    const auto rsp = client->Call(DeferredMessage::Method, nlohmann::json(req), 100);

    ASSERT_FALSE(rsp.has_value());
    EXPECT_EQ(rsp.error().code, static_cast<int>(ERROR_TIMEOUT));

    raw->Stop();
}
