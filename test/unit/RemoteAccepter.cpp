#include "RemoteAccepter.hpp"
#include "RemoteConnector.hpp"
#include <gtest/gtest.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>

namespace
{

/** Method of the server of a case which echoes its parameter. */
constexpr const char* kEchoMethod = "appbox_unit_echo";

/** Method of the server of a case which answers after a delay. */
constexpr const char* kDelayedMethod = "appbox_unit_delayed";

/** Method of the server of a case which reports the peer of its request. */
constexpr const char* kPeerMethod = "appbox_unit_peer";

/**
 * @brief Handler of a method which serves the session it is registered on.
 */
typedef std::function<void(const appbox::RemoteSession::Ptr&, uint64_t, const nlohmann::json&)> SessionHandler;

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
 * @brief Options of an accepter which races for the name of its pipe.
 * @return The options of the first instance of a pipe.
 */
appbox::RemoteAccepter::Options FirstInstanceOptions()
{
    appbox::RemoteAccepter::Options options;
    options.first_instance = true;
    return options;
}

/**
 * @brief Register a method on every session the accepter accepts.
 *
 * The accepter knows no method, so a case registers its handlers per session.
 * The handler is stored inside the session, so it holds the session weakly: a
 * strong reference would make the session own itself.
 *
 * @param[in] accepter Accepter to register the method on.
 * @param[in] method Name of the method.
 * @param[in] handler Implementation of the method.
 */
void RegisterOnSessions(const appbox::RemoteAccepter::Ptr& accepter, const std::string& method, SessionHandler handler)
{
    accepter->SetSessionCallback([method, handler](const appbox::RemoteSession::Ptr& session) {
        std::weak_ptr<appbox::RemoteSession> weak_session = session;
        session->RegisterMethod(method, [weak_session, handler](uint64_t id, const nlohmann::json& param) {
            if (auto bound = weak_session.lock())
            {
                handler(bound, id, param);
            }
        });
    });
}

/**
 * @brief Register the method of a case which echoes its parameter.
 * @param[in] accepter Accepter to register the method on.
 */
void RegisterEcho(const appbox::RemoteAccepter::Ptr& accepter)
{
    RegisterOnSessions(accepter, kEchoMethod,
                       [](const appbox::RemoteSession::Ptr& session, uint64_t id, const nlohmann::json& param) {
                           (void)session->SendResponse(id, param);
                       });
}

/**
 * @brief Client which writes and reads the frames of the transport itself.
 *
 * A session refuses to send a payload which is no request, so the cases which
 * exercise the robustness of the accepter against a malformed request talk to
 * the pipe directly.
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
 * @brief The first instance of a pipe is created, and the accepter serves.
 */
TEST(Unit_RemoteAccepter, FirstInstanceCreates)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, FirstInstanceOptions());

    ASSERT_TRUE(created.has_value());

    RegisterEcho(*created);
    ASSERT_TRUE((*created)->Start().has_value());

    auto connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(connector.has_value());
    auto connected = (*connector)->Connect(5000);
    ASSERT_TRUE(connected.has_value()) << static_cast<int>(connected.error());
    const auto& session = *connected;

    const auto rsp = session->Call(kEchoMethod, "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    (*created)->Stop();
}

/**
 * @brief A second process which races for the same name learns that an accepter
 *        listens on the pipe, and the first one keeps serving.
 */
TEST(Unit_RemoteAccepter, SecondInstanceReportsAlreadyRunning)
{
    const std::string pipe = MakePipePath();

    const auto first = appbox::RemoteAccepter::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_TRUE(first.has_value()) << static_cast<int>(first.error());
    ASSERT_TRUE((*first)->Start().has_value());

    const auto second = appbox::RemoteAccepter::CreateCompetitive(pipe, FirstInstanceOptions());

    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), static_cast<DWORD>(ERROR_ACCESS_DENIED));

    RegisterEcho(*first);

    auto connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(connector.has_value());
    auto connected = (*connector)->Connect(5000);
    ASSERT_TRUE(connected.has_value()) << static_cast<int>(connected.error());
    const auto& session = *connected;

    const auto rsp = session->Call(kEchoMethod, "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    (*first)->Stop();
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
TEST(Unit_RemoteAccepter, BusyPipeReportsAlreadyRunning)
{
    const std::string pipe = MakePipePath();

    /* One instance, which a client holds: every further connection is busy. */
    const HANDLE instance = ::CreateNamedPipeA(pipe.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT,
                                               PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
    ASSERT_NE(instance, INVALID_HANDLE_VALUE) << ::GetLastError();

    const HANDLE client =
        ::CreateFileA(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(client, INVALID_HANDLE_VALUE) << ::GetLastError();

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, FirstInstanceOptions());

    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error(), static_cast<DWORD>(ERROR_ACCESS_DENIED));

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
TEST(Unit_RemoteAccepter, MissingPipeIsNotReachable)
{
    EXPECT_FALSE(appbox::detail::AccepterPeerIsReachable(MakePipePath()));
}

/**
 * @brief A client which connects and leaves at once consumes the instance of
 *        the pipe, and the accepter keeps serving instead of ending its loop.
 *
 * The launcher of a sandbox probes the pipe of its sandbox exactly that way
 * when it races for the name, so the probe of a second launcher must not stop
 * the accepter of the first one.
 */
TEST(Unit_RemoteAccepter, AbandonedConnectionKeepsServing)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_TRUE(created.has_value()) << static_cast<int>(created.error());

    RegisterEcho(*created);
    ASSERT_TRUE((*created)->Start().has_value());

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

    auto connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(connector.has_value());
    auto connected = (*connector)->Connect(5000);
    ASSERT_TRUE(connected.has_value()) << static_cast<int>(connected.error());
    const auto& session = *connected;

    const auto rsp = session->Call(kEchoMethod, "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    (*created)->Stop();
}

/**
 * @brief Stopping refuses new connections, answers the requests which are in
 *        flight and may be repeated.
 */
TEST(Unit_RemoteAccepter, StopRefusesNewConnections)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_TRUE(created.has_value()) << static_cast<int>(created.error());

    std::atomic_bool entered = false;

    RegisterOnSessions((*created), kDelayedMethod,
                       [&entered](const appbox::RemoteSession::Ptr& session, uint64_t id, const nlohmann::json&) {
                           entered = true;
                           std::this_thread::sleep_for(std::chrono::milliseconds(100));
                           (void)session->SendResponse(id, "done");
                       });

    ASSERT_TRUE((*created)->Start().has_value());

    auto connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(connector.has_value());
    auto connected = (*connector)->Connect(5000);
    ASSERT_TRUE(connected.has_value()) << static_cast<int>(connected.error());
    const auto& session = *connected;

    auto future = session->Call(kDelayedMethod, nlohmann::json());

    /* Wait until the request is handled, then stop while it is in flight. */
    for (int i = 0; i < 400 && !entered.load(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(entered.load());

    (*created)->Stop();

    /* The answer of the request which was in flight reached the client. */
    const auto rsp = future.get();
    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "done");

    /* A client which connects afterwards is refused. */
    auto late = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(late.has_value());
    EXPECT_FALSE((*late)->Connect(300).has_value());

    /* The stop is idempotent. */
    (*created)->Stop();
}

/**
 * @brief The session reports the process at the other end of the connection of
 *        a request, which the authentication of the requests needs.
 */
TEST(Unit_RemoteAccepter, PeerProcessIdOfRequestIsAvailable)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_TRUE(created.has_value()) << static_cast<int>(created.error());

    RegisterOnSessions((*created), kPeerMethod,
                       [](const appbox::RemoteSession::Ptr& session, uint64_t id, const nlohmann::json&) {
                           (void)session->SendResponse(id, session->PeerProcessIdOfRequest(id));
                       });

    ASSERT_TRUE((*created)->Start().has_value());

    auto connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(connector.has_value());
    auto connected = (*connector)->Connect(5000);
    ASSERT_TRUE(connected.has_value()) << static_cast<int>(connected.error());
    const auto& session = *connected;

    const auto rsp = session->Call(kPeerMethod, nlohmann::json()).get();

    ASSERT_TRUE(rsp.has_value());

    /* The client of the case runs in the process of the test. */
    EXPECT_EQ(rsp.value().get<DWORD>(), ::GetCurrentProcessId());

    (*created)->Stop();
}

/**
 * @brief A payload which is no JSON is answered with the parse error, and the
 *        accepter keeps serving.
 */
TEST(Unit_RemoteAccepter, MalformedJsonKeepsServing)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_TRUE(created.has_value()) << static_cast<int>(created.error());

    RegisterEcho(*created);
    ASSERT_TRUE((*created)->Start().has_value());

    RawPipeClient raw(pipe);
    ASSERT_TRUE(raw.Valid());
    ASSERT_TRUE(raw.SendPayload("{ this is no json"));

    std::string payload;
    ASSERT_TRUE(raw.ReadPayload(payload, 5000));

    const auto rsp = nlohmann::json::parse(payload);
    ASSERT_TRUE(rsp.contains("error"));
    EXPECT_EQ(rsp["error"]["code"].get<int>(), -32700);
    EXPECT_TRUE(rsp["id"].is_null());

    /* The IO thread of the accepter survived the malformed request. */
    auto connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(connector.has_value());
    auto connected = (*connector)->Connect(5000);
    ASSERT_TRUE(connected.has_value()) << static_cast<int>(connected.error());
    const auto& session = *connected;

    const auto echoed = session->Call(kEchoMethod, "hello").get();
    ASSERT_TRUE(echoed.has_value());
    EXPECT_EQ(echoed.value().get<std::string>(), "hello");

    (*created)->Stop();
}

/**
 * @brief A method which is not registered is reported as a method error.
 */
TEST(Unit_RemoteAccepter, UnknownMethodReturnsError)
{
    const std::string pipe = MakePipePath();

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, FirstInstanceOptions());
    ASSERT_TRUE(created.has_value()) << static_cast<int>(created.error());
    ASSERT_TRUE((*created)->Start().has_value());

    auto connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(connector.has_value());
    auto connected = (*connector)->Connect(5000);
    ASSERT_TRUE(connected.has_value()) << static_cast<int>(connected.error());
    const auto& session = *connected;

    const auto rsp = session->Call("appbox_unit_missing_method", nlohmann::json::object()).get();

    ASSERT_FALSE(rsp.has_value());
    EXPECT_EQ(rsp.error().code, -32601);

    (*created)->Stop();
}

/**
 * @brief A request which exceeds the limit of the accepter is answered with the
 *        invalid request error, and the accepter keeps serving.
 *
 * The limit of the frame of the transport (64 MiB) is checked by the session,
 * which drops a stream that carries such a frame. The limit of a request is
 * checked by the session of the accepter as well, which can answer it.
 */
TEST(Unit_RemoteAccepter, OversizedFrameReturnsError)
{
    const std::string pipe = MakePipePath();

    /*
     * The limit has to stay above the size of a regular request of the case,
     * which is answered after the oversized one.
     */
    auto options = FirstInstanceOptions();
    options.max_request_size = 128;

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, options);
    ASSERT_TRUE(created.has_value()) << static_cast<int>(created.error());

    RegisterEcho(*created);
    ASSERT_TRUE((*created)->Start().has_value());

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

    /* The accepter still answers a request which is within the limit. */
    auto connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(connector.has_value());
    auto connected = (*connector)->Connect(5000);
    ASSERT_TRUE(connected.has_value()) << static_cast<int>(connected.error());
    const auto& session = *connected;

    const auto echoed = session->Call(kEchoMethod, "hello").get();
    ASSERT_TRUE(echoed.has_value());

    (*created)->Stop();
}

/**
 * @brief A transient failure of the creation of the next instance is retried,
 *        and the accepter keeps serving after the retries.
 */
TEST(Unit_RemoteAccepter, AcceptRetryKeepsServing)
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

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, options);
    ASSERT_TRUE(created.has_value()) << static_cast<int>(created.error());

    RegisterEcho(*created);

    std::atomic_int reported = 0;
    (*created)->SetAcceptFailureCallback([&reported](appbox::AcceptFailureKind, DWORD) { ++reported; });

    ASSERT_TRUE((*created)->Start().has_value());

    /* The connection of this client starts the failing creation of the next instance. */
    auto first_connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(first_connector.has_value());
    auto first_connected = (*first_connector)->Connect(5000);
    ASSERT_TRUE(first_connected.has_value()) << static_cast<int>(first_connected.error());

    /* The session stays connected for the whole case, its answer is not used. */
    [[maybe_unused]] const auto& first = *first_connected;

    /* The retries of the accepter run while this client waits for an instance. */
    auto second_connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(second_connector.has_value());
    auto second_connected = (*second_connector)->Connect(5000);
    ASSERT_TRUE(second_connected.has_value()) << static_cast<int>(second_connected.error());
    const auto& second = *second_connected;

    const auto rsp = second->Call(kEchoMethod, "hello").get();
    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    EXPECT_EQ(reported.load(), 0);

    (*created)->Stop();
}

/**
 * @brief A fatal failure of the accept loop is reported to the callback instead
 *        of ending the IO thread, and no client is accepted afterwards.
 */
TEST(Unit_RemoteAccepter, AcceptNextFailureIsReported)
{
    const std::string pipe = MakePipePath();

    auto options = FirstInstanceOptions();
    options.create_instance_error = []() -> DWORD { return ERROR_INVALID_HANDLE; };

    const auto created = appbox::RemoteAccepter::CreateCompetitive(pipe, options);
    ASSERT_TRUE(created.has_value()) << static_cast<int>(created.error());

    std::mutex                mutex;
    std::condition_variable   cv;
    bool                      reported = false;
    appbox::AcceptFailureKind kind = appbox::AcceptFailureKind::StillServing;
    DWORD                     code = 0;

    (*created)->SetAcceptFailureCallback([&](appbox::AcceptFailureKind failure, DWORD error) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            reported = true;
            kind = failure;
            code = error;
        }
        cv.notify_all();
    });

    ASSERT_TRUE((*created)->Start().has_value());

    /* The connection of this client starts the failing creation of the next instance. */
    auto connector = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(connector.has_value());
    auto connected = (*connector)->Connect(5000);
    ASSERT_TRUE(connected.has_value()) << static_cast<int>(connected.error());

    /* The session stays connected for the whole case, its answer is not used. */
    [[maybe_unused]] const auto& session = *connected;

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&reported] { return reported; }));
    }

    EXPECT_EQ(kind, appbox::AcceptFailureKind::NoInstance);
    EXPECT_EQ(code, static_cast<DWORD>(ERROR_INVALID_HANDLE));

    /* No instance waits for a client any more. */
    auto late = appbox::RemoteConnector::Create(pipe);
    ASSERT_TRUE(late.has_value());
    EXPECT_FALSE((*late)->Connect(300).has_value());

    (*created)->Stop();
}
