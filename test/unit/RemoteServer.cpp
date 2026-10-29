/*
 * The RPC headers pull in winsock2.h through asio, they have to come before
 * <windows.h> which would otherwise include the winsock.h version 1 header.
 */
#include "RemoteClient.hpp"
#include "RemoteServer.hpp"
#include <gtest/gtest.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>

namespace
{

/** Method of the server of a case which echoes its parameter. */
constexpr const char* kEchoMethod = "appbox_unit_echo";

/**
 * @brief Build a unique named pipe path.
 * @return The pipe path.
 */
std::string MakePipePath()
{
    std::random_device rd;
    std::mt19937_64    gen(rd());

    return R"(\\.\pipe\appbox_unit_srv_)" + std::to_string(gen());
}

/**
 * @brief Options of a server which races for the name of its pipe.
 * @return The options of the first instance of a pipe.
 */
appbox::RemoteServer::Options FirstInstanceOptions()
{
    appbox::RemoteServer::Options options;
    options.first_instance = true;
    return options;
}

/**
 * @brief Client which writes and reads the frames of the transport itself.
 *
 * The client of the module refuses to send a payload which is no request, so
 * the cases which exercise the robustness of the server against a malformed
 * request talk to the pipe directly.
 */
class RawPipeClient
{
public:
    /**
     * @brief Open the client end of a pipe.
     * @param[in] pipe Name of the pipe.
     */
    explicit RawPipeClient(const std::string& pipe)
    {
        handle_ = ::CreateFileA(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    }

    ~RawPipeClient()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(handle_);
        }
    }

    /**
     * @brief Whether the pipe was opened.
     * @return true when the handle is usable.
     */
    bool Valid() const
    {
        return handle_ != INVALID_HANDLE_VALUE;
    }

    /**
     * @brief Write one frame with the given payload.
     * @param[in] payload Payload of the frame.
     * @return true on success, otherwise false.
     */
    bool SendPayload(const std::string& payload)
    {
        if (!Valid())
        {
            return false;
        }

        appbox::RemoteProtocol head;
        head.length = static_cast<uint32_t>(payload.size());

        DWORD written = 0;
        if (!::WriteFile(handle_, &head, sizeof(head), &written, nullptr) || written != sizeof(head))
        {
            return false;
        }

        if (payload.empty())
        {
            return true;
        }

        written = 0;
        return ::WriteFile(handle_, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) != FALSE &&
               written == payload.size();
    }

    /**
     * @brief Read one frame which the server sends.
     * @param[out] payload Payload of the frame.
     * @param[in] timeout_ms Budget of the wait.
     * @return true when a whole frame was read, otherwise false.
     */
    bool ReadPayload(std::string& payload, uint32_t timeout_ms)
    {
        if (!Valid())
        {
            return false;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        appbox::RemoteProtocol head;
        if (!WaitForBytes(sizeof(head), deadline) || !ReadExact(&head, sizeof(head)))
        {
            return false;
        }

        payload.assign(head.length, '\0');
        if (head.length == 0)
        {
            return true;
        }

        return WaitForBytes(head.length, deadline) && ReadExact(payload.data(), head.length);
    }

private:
    /**
     * @brief Wait until the pipe holds at least the given number of bytes.
     * @param[in] count Number of bytes.
     * @param[in] deadline Point in time at which the wait gives up.
     * @return true when the bytes are available, otherwise false.
     */
    bool WaitForBytes(DWORD count, std::chrono::steady_clock::time_point deadline)
    {
        for (;;)
        {
            DWORD available = 0;
            if (::PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr) && available >= count)
            {
                return true;
            }

            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }

            ::Sleep(5);
        }
    }

    /**
     * @brief Read the given number of bytes from the pipe.
     * @param[out] buffer Destination of the bytes.
     * @param[in] size Number of bytes.
     * @return true on success, otherwise false.
     */
    bool ReadExact(void* buffer, DWORD size)
    {
        auto* out = static_cast<char*>(buffer);
        DWORD done = 0;
        while (done < size)
        {
            DWORD read = 0;
            if (!::ReadFile(handle_, out + done, size - done, &read, nullptr) || read == 0)
            {
                return false;
            }
            done += read;
        }

        return true;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE; /* Client end of the pipe */
};

} // namespace

/**
 * @brief The first instance of a pipe is created, and the server answers.
 */
TEST(Unit_RemoteServer, FirstInstanceCreates)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, FirstInstanceOptions());

    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created);
    ASSERT_NE(created.server, nullptr);
    EXPECT_EQ(created.error_code, static_cast<DWORD>(ERROR_SUCCESS));

    appbox::RemoteServer* server = created.server.get();
    server->RegisterMethod(kEchoMethod,
                           [server](uint64_t id, const nlohmann::json& param) { server->SendResponse(id, param); });

    ASSERT_TRUE(server->Start());

    auto client = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(client->Start(5000));

    const auto rsp = client->Call(kEchoMethod, "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    server->Stop();
}

/**
 * @brief A second process which races for the same name learns that a server
 *        listens on the pipe, and the first server keeps serving.
 */
TEST(Unit_RemoteServer, SecondInstanceReportsAlreadyRunning)
{
    const std::string pipe = MakePipePath();

    const auto first = appbox::RemoteServer::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_EQ(first.status, appbox::ServerCreateStatus::Created);
    ASSERT_NE(first.server, nullptr);
    ASSERT_TRUE(first.server->Start());

    const auto second = appbox::RemoteServer::CreateCompetitive(pipe, FirstInstanceOptions());

    EXPECT_EQ(second.status, appbox::ServerCreateStatus::AlreadyRunning);
    EXPECT_EQ(second.server, nullptr);
    EXPECT_EQ(second.error_code, static_cast<DWORD>(ERROR_ACCESS_DENIED));

    appbox::RemoteServer* server = first.server.get();
    server->RegisterMethod(kEchoMethod,
                           [server](uint64_t id, const nlohmann::json& param) { server->SendResponse(id, param); });

    auto client = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(client->Start(5000));

    const auto rsp = client->Call(kEchoMethod, "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    server->Stop();
}

/**
 * @brief A pipe whose only instance serves a client is a running server.
 *
 * The creation of the first instance tells a name which is taken from a name
 * which is free, and the two reasons of an access denial are told apart by a
 * connection to the pipe. A pipe whose instances all serve a client refuses
 * that connection with `ERROR_PIPE_BUSY`, which says nothing about the server:
 * a launcher which loses the race while the master instance is busy has to hand
 * its request over instead of reporting a pipe of another user.
 *
 * The pipe of the case is created the way the launcher creates its control
 * pipe, with an unbounded number of instances: the instance count is what
 * decides the error of a second creation, and a bounded count reports the busy
 * pipe instead of the taken name.
 */
TEST(Unit_RemoteServer, BusyPipeReportsAlreadyRunning)
{
    const std::string pipe = MakePipePath();

    /* One instance, which a client holds: every further connection is busy. */
    const HANDLE instance = ::CreateNamedPipeA(pipe.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT,
                                               PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
    ASSERT_NE(instance, INVALID_HANDLE_VALUE) << ::GetLastError();

    const HANDLE client =
        ::CreateFileA(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(client, INVALID_HANDLE_VALUE) << ::GetLastError();

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, FirstInstanceOptions());

    EXPECT_EQ(created.status, appbox::ServerCreateStatus::AlreadyRunning);
    EXPECT_EQ(created.server, nullptr);
    EXPECT_EQ(created.error_code, static_cast<DWORD>(ERROR_ACCESS_DENIED));

    ::CloseHandle(client);
    ::CloseHandle(instance);
}

/**
 * @brief A name which carries no pipe at all is not reachable.
 *
 * The access denial of the creation is a pipe of another user only when nothing
 * of this user holds the name, so the probe has to report the other direction
 * as well.
 */
TEST(Unit_RemoteServer, MissingPipeIsNotReachable)
{
    EXPECT_FALSE(appbox::detail::RemoteServerPeerIsReachable(MakePipePath()));
}

/**
 * @brief A client which connects and leaves at once consumes the instance of
 *        the pipe, and the server keeps serving instead of ending its loop.
 *
 * The launcher of a sandbox probes the pipe of its sandbox exactly that way
 * when it races for the name, so the probe of a second launcher must not stop
 * the server of the first one.
 */
TEST(Unit_RemoteServer, AbandonedConnectionKeepsServing)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created);
    ASSERT_NE(created.server, nullptr);

    appbox::RemoteServer* server = created.server.get();
    server->RegisterMethod(kEchoMethod,
                           [server](uint64_t id, const nlohmann::json& param) { server->SendResponse(id, param); });
    ASSERT_TRUE(server->Start());

    /* Connect and leave again, which is how a launcher probes the name. */
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const HANDLE probe =
            ::CreateFileA(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (probe != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(probe);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    auto client = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(client->Start(5000));

    const auto rsp = client->Call(kEchoMethod, "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    server->Stop();
}

/**
 * @brief An access denial of the creation is a running server only when the
 *        pipe can be connected.
 */
TEST(Unit_RemoteServer, CreateResultMapsAccessDenied)
{
    using appbox::ServerCreateStatus;

    EXPECT_EQ(appbox::detail::ClassifyRemoteServerCreateError(ERROR_SUCCESS, false), ServerCreateStatus::Created);
    EXPECT_EQ(appbox::detail::ClassifyRemoteServerCreateError(ERROR_SUCCESS, true), ServerCreateStatus::Created);

    EXPECT_EQ(appbox::detail::ClassifyRemoteServerCreateError(ERROR_ACCESS_DENIED, true),
              ServerCreateStatus::AlreadyRunning);
    EXPECT_EQ(appbox::detail::ClassifyRemoteServerCreateError(ERROR_ACCESS_DENIED, false),
              ServerCreateStatus::AccessDenied);

    EXPECT_EQ(appbox::detail::ClassifyRemoteServerCreateError(ERROR_NOT_ENOUGH_MEMORY, false),
              ServerCreateStatus::Failed);
    EXPECT_EQ(appbox::detail::ClassifyRemoteServerCreateError(ERROR_INVALID_NAME, true), ServerCreateStatus::Failed);
}

/**
 * @brief Stopping refuses new connections, answers the requests which are in
 *        flight and may be repeated.
 */
TEST(Unit_RemoteServer, StopRefusesNewConnections)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created);

    appbox::RemoteServer* server = created.server.get();
    std::atomic_bool      entered = false;

    server->RegisterMethod("appbox_unit_delayed", [server, &entered](uint64_t id, const nlohmann::json&) {
        entered = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server->SendResponse(id, "done");
    });

    ASSERT_TRUE(server->Start());

    auto client = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(client->Start(5000));

    auto future = client->Call("appbox_unit_delayed", nlohmann::json());

    /* Wait until the request is handled, then stop while it is in flight. */
    for (int i = 0; i < 400 && !entered.load(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(entered.load());

    server->Stop();

    /* The answer of the request which was in flight reached the client. */
    const auto rsp = future.get();
    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "done");

    /* A client which connects afterwards is refused. */
    auto late = appbox::RemoteClient::Create(pipe);
    EXPECT_FALSE(late->Start(300));

    /* The stop is idempotent. */
    server->Stop();
}

/**
 * @brief The server reports the process at the other end of the session of a
 *        request, which the authentication of the requests needs.
 */
TEST(Unit_RemoteServer, PeerProcessIdOfRequestIsAvailable)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created);

    appbox::RemoteServer* server = created.server.get();
    server->RegisterMethod("appbox_unit_peer", [server](uint64_t id, const nlohmann::json&) {
        server->SendResponse(id, server->PeerProcessIdOfRequest(id));
    });

    ASSERT_TRUE(server->Start());

    auto client = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(client->Start(5000));

    const auto rsp = client->Call("appbox_unit_peer", nlohmann::json()).get();

    ASSERT_TRUE(rsp.has_value());

    /* The client of the case runs in the process of the test. */
    EXPECT_EQ(rsp.value().get<DWORD>(), ::GetCurrentProcessId());

    server->Stop();
}

/**
 * @brief A payload which is no JSON is answered with the parse error, and the
 *        server keeps serving.
 */
TEST(Unit_RemoteServer, MalformedJsonKeepsServing)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created);

    appbox::RemoteServer* server = created.server.get();
    server->RegisterMethod(kEchoMethod,
                           [server](uint64_t id, const nlohmann::json& param) { server->SendResponse(id, param); });

    ASSERT_TRUE(server->Start());

    RawPipeClient raw(pipe);
    ASSERT_TRUE(raw.Valid());
    ASSERT_TRUE(raw.SendPayload("{ this is no json"));

    std::string payload;
    ASSERT_TRUE(raw.ReadPayload(payload, 5000));

    const auto rsp = nlohmann::json::parse(payload);
    ASSERT_TRUE(rsp.contains("error"));
    EXPECT_EQ(rsp["error"]["code"].get<int>(), -32700);
    EXPECT_TRUE(rsp["id"].is_null());

    /* The IO thread of the server survived the malformed request. */
    auto client = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(client->Start(5000));

    const auto echoed = client->Call(kEchoMethod, "hello").get();
    ASSERT_TRUE(echoed.has_value());
    EXPECT_EQ(echoed.value().get<std::string>(), "hello");

    server->Stop();
}

/**
 * @brief A method which is not registered is reported as a method error.
 */
TEST(Unit_RemoteServer, UnknownMethodReturnsError)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created);
    ASSERT_TRUE(created.server->Start());

    auto client = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(client->Start(5000));

    const auto rsp = client->Call("appbox_unit_missing_method", nlohmann::json::object()).get();

    ASSERT_FALSE(rsp.has_value());
    EXPECT_EQ(rsp.error().code, -32601);

    created.server->Stop();
}

/**
 * @brief A request which exceeds the limit of the server is answered with the
 *        invalid request error, and the server keeps serving.
 *
 * The limit of the frame of the transport (64 MiB) is checked by the session,
 * which drops a stream that carries such a frame. The limit of a request is
 * checked by the server, which can answer it.
 */
TEST(Unit_RemoteServer, OversizedFrameReturnsError)
{
    const std::string pipe = MakePipePath();

    /*
     * The limit has to stay above the size of a regular request of the case,
     * which is answered after the oversized one.
     */
    auto options = FirstInstanceOptions();
    options.max_request_size = 128;

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, options);
    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created);

    appbox::RemoteServer* server = created.server.get();
    server->RegisterMethod(kEchoMethod,
                           [server](uint64_t id, const nlohmann::json& param) { server->SendResponse(id, param); });

    ASSERT_TRUE(server->Start());

    RawPipeClient raw(pipe);
    ASSERT_TRUE(raw.Valid());

    const std::string request =
        std::string(R"({"id":1,"method":"appbox_unit_echo","params":")") + std::string(200, 'x') + "\"}";
    ASSERT_GT(request.size(), options.max_request_size);
    ASSERT_TRUE(raw.SendPayload(request));

    std::string payload;
    ASSERT_TRUE(raw.ReadPayload(payload, 5000));

    const auto rsp = nlohmann::json::parse(payload);
    ASSERT_TRUE(rsp.contains("error"));
    EXPECT_EQ(rsp["error"]["code"].get<int>(), -32600);

    /* The server still answers a request which is within the limit. */
    auto client = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(client->Start(5000));

    const auto echoed = client->Call(kEchoMethod, "hello").get();
    ASSERT_TRUE(echoed.has_value());

    server->Stop();
}

/**
 * @brief A transient failure of the creation of the next instance is retried,
 *        and the server keeps serving after the retries.
 */
TEST(Unit_RemoteServer, AcceptRetryKeepsServing)
{
    const std::string pipe = MakePipePath();

    auto failures = std::make_shared<std::atomic_int>(2);

    auto options = FirstInstanceOptions();
    options.create_instance_error = [failures]() -> DWORD {
        if (failures->fetch_sub(1) > 0)
        {
            return ERROR_NO_SYSTEM_RESOURCES;
        }
        return ERROR_SUCCESS;
    };

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, options);
    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created);

    appbox::RemoteServer* server = created.server.get();
    server->RegisterMethod(kEchoMethod,
                           [server](uint64_t id, const nlohmann::json& param) { server->SendResponse(id, param); });

    std::atomic_int reported = 0;
    server->SetAcceptFailureCallback([&reported](appbox::AcceptFailureKind, DWORD) { ++reported; });

    ASSERT_TRUE(server->Start());

    /* The connection of this client starts the failing creation of the next instance. */
    auto first = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(first->Start(5000));

    /* The retries of the server run while this client waits for an instance. */
    auto second = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(second->Start(5000));

    const auto rsp = second->Call(kEchoMethod, "hello").get();
    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    EXPECT_EQ(reported.load(), 0);

    server->Stop();
}

/**
 * @brief A fatal failure of the accept loop is reported to the callback instead
 *        of ending the IO thread, and no client is accepted afterwards.
 */
TEST(Unit_RemoteServer, AcceptNextFailureIsReported)
{
    const std::string pipe = MakePipePath();

    auto options = FirstInstanceOptions();
    options.create_instance_error = []() -> DWORD { return ERROR_INVALID_HANDLE; };

    const auto created = appbox::RemoteServer::CreateCompetitive(pipe, options);
    ASSERT_EQ(created.status, appbox::ServerCreateStatus::Created);

    appbox::RemoteServer* server = created.server.get();

    std::mutex                mutex;
    std::condition_variable   cv;
    bool                      reported = false;
    appbox::AcceptFailureKind kind = appbox::AcceptFailureKind::StillServing;
    DWORD                     code = 0;

    server->SetAcceptFailureCallback([&](appbox::AcceptFailureKind failure, DWORD error) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            reported = true;
            kind = failure;
            code = error;
        }
        cv.notify_all();
    });

    ASSERT_TRUE(server->Start());

    /* The connection of this client starts the failing creation of the next instance. */
    auto client = appbox::RemoteClient::Create(pipe);
    ASSERT_TRUE(client->Start(5000));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&reported] { return reported; }));
    }

    EXPECT_EQ(kind, appbox::AcceptFailureKind::NoInstance);
    EXPECT_EQ(code, static_cast<DWORD>(ERROR_INVALID_HANDLE));

    /* No instance waits for a client any more. */
    auto late = appbox::RemoteClient::Create(pipe);
    EXPECT_FALSE(late->Start(300));

    server->Stop();
}
