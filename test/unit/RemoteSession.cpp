/*
 * The RPC headers pull in winsock2.h through asio, they have to come before
 * <windows.h> which would otherwise include the winsock.h version 1 header.
 */
#include "RemoteAccepter.hpp"
#include "RemoteConnector.hpp"
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
#include <vector>

namespace
{

/** Method which answers at once with its parameter. */
constexpr const char* kPingMethod = "appbox_unit_ping";

/** Method which answers after a delay, so its caller has a request in flight. */
constexpr const char* kSlowMethod = "appbox_unit_slow";

/** Method which answers with the number of the session it is registered on. */
constexpr const char* kTagMethod = "appbox_unit_tag";

/**
 * @brief Build a unique named pipe path.
 * @return The pipe path.
 */
std::string MakePipePath()
{
    std::random_device rd;
    std::mt19937_64    gen(rd());

    return R"(\\.\pipe\appbox_unit_sess_)" + std::to_string(gen());
}

/**
 * @brief Register a method which answers with its parameter on one session.
 * @param[in] session Session to register the method on.
 * @param[in] method Name of the method.
 */
void RegisterPing(const appbox::RemoteSession::Ptr& session, const std::string& method)
{
    std::weak_ptr<appbox::RemoteSession> weak_session = session;
    session->RegisterMethod(method, [weak_session](uint64_t id, const nlohmann::json& param) {
        if (auto bound = weak_session.lock())
        {
            bound->SendResponse(id, param);
        }
    });
}

} // namespace

/**
 * @brief A session answers a request of its peer while a request of its own is
 *        still pending.
 *
 * The two directions of one connection are independent: the read loop dispatches
 * a request to its handler and keeps reading, so a peer which is waiting for an
 * answer still serves the requests it receives.
 */
TEST(Unit_RemoteSession, AnswersWhileOwnRequestIsPending)
{
    const std::string pipe = MakePipePath();

    std::mutex                 mutex;
    std::condition_variable    cv;
    appbox::RemoteSession::Ptr accepted;

    auto accepter = appbox::RemoteAccepter::Create(pipe);
    accepter->SetSessionCallback([&](const appbox::RemoteSession::Ptr& session) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            accepted = session;
        }
        cv.notify_all();

        std::weak_ptr<appbox::RemoteSession> weak_session = session;
        session->RegisterMethod(kSlowMethod, [weak_session](uint64_t id, const nlohmann::json&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            if (auto bound = weak_session.lock())
            {
                bound->SendResponse(id, "slow");
            }
        });
    });
    ASSERT_TRUE(accepter->Start());

    auto connector = appbox::RemoteConnector::Create(pipe);
    auto session = connector->Connect(5000);
    ASSERT_NE(session, nullptr);

    /* The client answers the requests the accepter sends it. */
    RegisterPing(session, kPingMethod);

    /* The request of the client stays in flight while the accepter asks it. */
    auto pending = session->Call(kSlowMethod, nlohmann::json());

    appbox::RemoteSession::Ptr peer;
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&accepted] { return accepted != nullptr; }));
        peer = accepted;
    }

    const auto answer = peer->Call(kPingMethod, "ping", 5000);
    ASSERT_TRUE(answer.has_value()) << answer.error().message;
    EXPECT_EQ(answer.value().get<std::string>(), "ping");

    const auto slow = pending.get();
    ASSERT_TRUE(slow.has_value());
    EXPECT_EQ(slow.value().get<std::string>(), "slow");

    accepter->Stop();
}

/**
 * @brief An answer which arrives after the budget of its caller is dropped
 *        without breaking the session.
 */
TEST(Unit_RemoteSession, LateAnswerIsDropped)
{
    const std::string pipe = MakePipePath();

    std::mutex                 mutex;
    std::condition_variable    cv;
    appbox::RemoteSession::Ptr accepted;

    auto accepter = appbox::RemoteAccepter::Create(pipe);
    accepter->SetSessionCallback([&](const appbox::RemoteSession::Ptr& session) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            accepted = session;
        }
        cv.notify_all();
    });
    ASSERT_TRUE(accepter->Start());

    auto connector = appbox::RemoteConnector::Create(pipe);
    auto session = connector->Connect(5000);
    ASSERT_NE(session, nullptr);

    /* The client answers slowly, so the answer of the peer is late. */
    std::weak_ptr<appbox::RemoteSession> weak_session = session;
    session->RegisterMethod(kSlowMethod, [weak_session](uint64_t id, const nlohmann::json&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        if (auto bound = weak_session.lock())
        {
            bound->SendResponse(id, "late");
        }
    });
    RegisterPing(session, kPingMethod);

    appbox::RemoteSession::Ptr peer;
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&accepted] { return accepted != nullptr; }));
        peer = accepted;
    }

    /* The caller gives up before the answer is produced. */
    const auto late = peer->Call(kSlowMethod, nlohmann::json(), 50);
    ASSERT_FALSE(late.has_value());
    EXPECT_EQ(late.error().code, static_cast<int>(ERROR_TIMEOUT));

    /* The late answer is discarded, and the session keeps serving. */
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const auto ping = peer->Call(kPingMethod, "ping", 5000);
    ASSERT_TRUE(ping.has_value()) << ping.error().message;
    EXPECT_EQ(ping.value().get<std::string>(), "ping");

    accepter->Stop();
}

/**
 * @brief Two sessions of one accepter do not share their request tables.
 *
 * The request ids of a session are its own: two peers of one accepter number
 * their requests from the same start, so a table which the connections shared
 * would deliver the answer of one peer to the caller of the other.
 */
TEST(Unit_RemoteSession, SessionsDoNotShareRequestTables)
{
    const std::string pipe = MakePipePath();

    std::atomic_int accepted_count = 0;

    auto accepter = appbox::RemoteAccepter::Create(pipe);
    accepter->SetSessionCallback([&accepted_count](const appbox::RemoteSession::Ptr& session) {
        const int tag = accepted_count.fetch_add(1);

        std::weak_ptr<appbox::RemoteSession> weak_session = session;
        session->RegisterMethod(kTagMethod, [weak_session, tag](uint64_t id, const nlohmann::json&) {
            /* The delay keeps both requests of the case in flight at once. */
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (auto bound = weak_session.lock())
            {
                bound->SendResponse(id, tag);
            }
        });
    });
    ASSERT_TRUE(accepter->Start());

    auto first_connector = appbox::RemoteConnector::Create(pipe);
    auto first = first_connector->Connect(5000);
    ASSERT_NE(first, nullptr);

    auto second_connector = appbox::RemoteConnector::Create(pipe);
    auto second = second_connector->Connect(5000);
    ASSERT_NE(second, nullptr);

    /* Both requests carry the first id of their own session. */
    auto first_pending = first->Call(kTagMethod, nlohmann::json());
    auto second_pending = second->Call(kTagMethod, nlohmann::json());

    const auto first_rsp = first_pending.get();
    const auto second_rsp = second_pending.get();

    ASSERT_TRUE(first_rsp.has_value());
    ASSERT_TRUE(second_rsp.has_value());

    EXPECT_EQ(first_rsp.value().get<int>(), 0);
    EXPECT_EQ(second_rsp.value().get<int>(), 1);

    accepter->Stop();
}
