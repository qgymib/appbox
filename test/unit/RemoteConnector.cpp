#include "RemoteAccepter.hpp"
#include "RemoteConnector.hpp"
#include <gtest/gtest.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{

/** Method of an accepter which echoes its parameter. */
constexpr const char* kEchoMethod = "appbox_unit_echo";

/** Method of an accepter which answers after a delay. */
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
 * @brief Handler of a method which serves the session it is registered on.
 */
typedef std::function<void(const appbox::RemoteSession::Ptr&, uint64_t, const nlohmann::json&)> SessionHandler;

/**
 * @brief One method the accepter of a case serves.
 */
struct Method
{
    std::string    name;    /* Name of the method */
    SessionHandler handler; /* Implementation of the method */
};

/**
 * @brief Register the given methods on every session the accepter accepts.
 *
 * The accepter carries one session callback, so a case which needs several
 * methods registers them with one call.
 *
 * @param[in] accepter Accepter to register the methods on.
 * @param[in] methods Methods to serve.
 */
void RegisterOnSessions(const appbox::RemoteAccepter::Ptr& accepter, const std::vector<Method>& methods)
{
    accepter->SetSessionCallback([methods](const appbox::RemoteSession::Ptr& session) {
        std::weak_ptr<appbox::RemoteSession> weak_session = session;
        for (const auto& method : methods)
        {
            const SessionHandler handler = method.handler;
            session->RegisterMethod(method.name, [weak_session, handler](uint64_t id, const nlohmann::json& param) {
                if (auto bound = weak_session.lock())
                {
                    handler(bound, id, param);
                }
            });
        }
    });
}

/**
 * @brief Answer a request with the parameter it carries.
 * @param[in] session Session which received the request.
 * @param[in] id Id of the request.
 * @param[in] param Parameter of the request.
 */
void Echo(const appbox::RemoteSession::Ptr& session, uint64_t id, const nlohmann::json& param)
{
    session->SendResponse(id, param);
}

/**
 * @brief Register the method of an accepter which echoes its parameter.
 * @param[in] accepter Accepter to register the method on.
 */
void RegisterEcho(const appbox::RemoteAccepter::Ptr& accepter)
{
    RegisterOnSessions(accepter, {
                                     { kEchoMethod, Echo }
    });
}

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
TEST(Unit_RemoteConnector, StartOnRegularFileFailsWithoutLeakingHandle)
{
    const std::string file_path = MakeTempFilePath();
    CreateRegularFile(file_path);

    const DWORD before = CountHandles();

    {
        auto connector = appbox::RemoteConnector::Create(file_path);
        EXPECT_EQ(connector->Connect(), nullptr);
    }

    const DWORD after = CountHandles();
    EXPECT_LE(after, before + 1); /* Allow a single handle of noise. */

    std::filesystem::remove(file_path);
}

/**
 * @brief Repeated failing connections do not accumulate handles.
 */
TEST(Unit_RemoteConnector, RepeatedFailedStartDoesNotLeakHandles)
{
    const std::string file_path = MakeTempFilePath();
    CreateRegularFile(file_path);

    /* Warm up, the first attempt may create lazily initialized resources. */
    {
        auto connector = appbox::RemoteConnector::Create(file_path);
        EXPECT_EQ(connector->Connect(), nullptr);
    }

    const DWORD before = CountHandles();

    for (int i = 0; i < 10; ++i)
    {
        auto connector = appbox::RemoteConnector::Create(file_path);
        EXPECT_EQ(connector->Connect(), nullptr);
    }

    const DWORD after = CountHandles();
    EXPECT_LE(after, before + 1);

    std::filesystem::remove(file_path);
}

/**
 * @brief A pipe which does not exist makes the connect fail.
 */
TEST(Unit_RemoteConnector, StartOnMissingPipeFails)
{
    auto connector = appbox::RemoteConnector::Create(MakePipePath());

    /* The budget of the attempts is short, no server will ever appear. */
    EXPECT_EQ(connector->Connect(200), nullptr);
}

/**
 * @brief An error response of the accepter is delivered to the caller instead of
 *        being dropped or throwing.
 */
TEST(Unit_RemoteConnector, CallUnknownMethodReturnsError)
{
    const std::string pipe_path = MakePipePath();

    auto accepter = appbox::RemoteAccepter::Create(pipe_path);
    ASSERT_TRUE(accepter->Start());

    auto connector = appbox::RemoteConnector::Create(pipe_path);
    auto session = connector->Connect();
    ASSERT_NE(session, nullptr);

    auto rsp = session->Call("appbox_unit_missing_method", nlohmann::json::object()).get();

    ASSERT_FALSE(rsp.has_value());
    EXPECT_EQ(rsp.error().code, -32601);

    accepter->Stop();
}

/**
 * @brief A regular request and response round trip still works.
 */
TEST(Unit_RemoteConnector, CallRegisteredMethod)
{
    const std::string pipe_path = MakePipePath();

    auto accepter = appbox::RemoteAccepter::Create(pipe_path);
    RegisterEcho(accepter);
    ASSERT_TRUE(accepter->Start());

    auto connector = appbox::RemoteConnector::Create(pipe_path);
    auto session = connector->Connect();
    ASSERT_NE(session, nullptr);

    auto rsp = session->Call(kEchoMethod, "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    accepter->Stop();
}

/**
 * @brief A response which does not arrive in the budget of the call is reported
 *        as a timeout, and the session keeps working afterwards.
 */
TEST(Unit_RemoteConnector, CallTimesOut)
{
    const std::string pipe_path = MakePipePath();

    auto accepter = appbox::RemoteAccepter::Create(pipe_path);
    RegisterOnSessions(accepter,
                       {
                           { kSlowMethod,
                            [](const appbox::RemoteSession::Ptr& session, uint64_t id, const nlohmann::json&) {
                                 std::this_thread::sleep_for(std::chrono::milliseconds(400));
                                 session->SendResponse(id, "late");
                             } },
                           { kEchoMethod, Echo }
    });
    ASSERT_TRUE(accepter->Start());

    auto connector = appbox::RemoteConnector::Create(pipe_path);
    auto session = connector->Connect(5000);
    ASSERT_NE(session, nullptr);

    const auto started = std::chrono::steady_clock::now();
    const auto rsp = session->Call(kSlowMethod, nlohmann::json(), 100);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

    ASSERT_FALSE(rsp.has_value());
    EXPECT_EQ(rsp.error().code, static_cast<int>(ERROR_TIMEOUT));
    EXPECT_LT(elapsed.count(), 1000); /* The call does not wait for the answer. */

    /* The session keeps working after the request of the client was dropped. */
    const auto echoed = session->Call(kEchoMethod, "hello", 5000);
    ASSERT_TRUE(echoed.has_value());
    EXPECT_EQ(echoed.value().get<std::string>(), "hello");

    accepter->Stop();
}

/**
 * @brief The errors of a pipe which is not there yet are retried, so a client
 *        may connect before its accepter does.
 */
TEST(Unit_RemoteConnector, RetriesOnBrokenPipe)
{
    EXPECT_TRUE(appbox::detail::IsRetryableConnectorConnectError(ERROR_FILE_NOT_FOUND));
    EXPECT_TRUE(appbox::detail::IsRetryableConnectorConnectError(ERROR_PIPE_BUSY));
    EXPECT_TRUE(appbox::detail::IsRetryableConnectorConnectError(ERROR_PIPE_NOT_CONNECTED));
    EXPECT_TRUE(appbox::detail::IsRetryableConnectorConnectError(ERROR_BROKEN_PIPE));

    EXPECT_FALSE(appbox::detail::IsRetryableConnectorConnectError(ERROR_ACCESS_DENIED));
    EXPECT_FALSE(appbox::detail::IsRetryableConnectorConnectError(ERROR_INVALID_NAME));

    const std::string pipe_path = MakePipePath();

    /* The accepter appears while the connector is already retrying. */
    appbox::RemoteAccepter::Ptr accepter;
    std::thread                 late_accepter([&accepter, &pipe_path]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        accepter = appbox::RemoteAccepter::Create(pipe_path);
        RegisterEcho(accepter);
        accepter->Start();
    });

    auto       connector = appbox::RemoteConnector::Create(pipe_path);
    const auto session = connector->Connect(5000);

    late_accepter.join();

    ASSERT_NE(session, nullptr);
    ASSERT_NE(accepter, nullptr);

    const auto rsp = session->Call(kEchoMethod, "hello").get();

    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp.value().get<std::string>(), "hello");

    accepter->Stop();
}

/**
 * @brief The connector reports the process of the accepter it is connected to.
 */
TEST(Unit_RemoteConnector, ServerProcessIdIsAvailable)
{
    const std::string pipe_path = MakePipePath();

    auto accepter = appbox::RemoteAccepter::Create(pipe_path);
    ASSERT_TRUE(accepter->Start());

    auto connector = appbox::RemoteConnector::Create(pipe_path);
    auto session = connector->Connect(5000);
    ASSERT_NE(session, nullptr);

    /* The accepter of the case runs in the process of the test. */
    EXPECT_EQ(connector->ServerProcessId(), ::GetCurrentProcessId());
    EXPECT_EQ(session->PeerProcessId(), ::GetCurrentProcessId());

    accepter->Stop();
}

/**
 * @brief The strong-typed call carries a budget: an answer which arrives after
 *        the budget is dropped and the session keeps working.
 *
 * The wrapper of the former client was a deferred task, which could not be
 * waited for with a budget at all. The session answers a request with an
 * `expected` of the protocol, so the caller decides how long it waits.
 */
TEST(Unit_RemoteConnector, StrongTypedCallHonoursBudget)
{
    const std::string pipe_path = MakePipePath();

    auto accepter = appbox::RemoteAccepter::Create(pipe_path);
    RegisterOnSessions(accepter,
                       {
                           { DeferredMessage::Method,
                            [](const appbox::RemoteSession::Ptr& session, uint64_t id, const nlohmann::json&) {
                                 std::this_thread::sleep_for(std::chrono::milliseconds(300));
                                 session->SendResponse(id, DeferredMessage::Rsp{ "late" });
                             } },
                           { kEchoMethod, Echo }
    });
    ASSERT_TRUE(accepter->Start());

    auto connector = appbox::RemoteConnector::Create(pipe_path);
    auto session = connector->Connect(5000);
    ASSERT_NE(session, nullptr);

    DeferredMessage::Req req;
    req.value = "hello";

    const auto started = std::chrono::steady_clock::now();
    const auto rsp = session->Call<DeferredMessage>(req, 100);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

    ASSERT_FALSE(rsp.has_value());
    EXPECT_EQ(rsp.error().code, static_cast<int>(ERROR_TIMEOUT));
    EXPECT_LT(elapsed.count(), 1000);

    /*
     * The answer of the request arrives after the budget of its caller: it is
     * dropped, and the session answers the next request as if nothing happened.
     */
    const auto echoed = session->Call(kEchoMethod, "hello", 5000);
    ASSERT_TRUE(echoed.has_value());
    EXPECT_EQ(echoed.value().get<std::string>(), "hello");

    accepter->Stop();
}
