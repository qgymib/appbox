#include "utils/WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "utils/Log.hpp"

namespace
{

/* Number of messages received by the test sink. */
std::atomic<int> g_received = 0;

/* Result returned by the test sink. */
std::atomic<bool> g_sink_result = true;

/* Threads and iterations used by the concurrency test. */
constexpr int kConcurrentThreads = 4;
constexpr int kConcurrentIterations = 200;

/**
 * @brief Install a test sink which counts the received messages.
 * @param[in] result Result the sink returns to the caller.
 */
void InstallCountingSink(bool result)
{
    g_received = 0;
    g_sink_result = result;

    appbox::SetLogSink([](const appbox::MsgLog::Req&, nlohmann::json&) {
        ++g_received;
        return g_sink_result.load();
    });
}

} // namespace

/**
 * @brief Without a sink the message is dropped, the log path must neither crash
 *        nor throw. This is the state outside isolation mode, where the sandbox
 *        has no RPC client at all.
 */
TEST(UnitLog, LogWithoutSinkDoesNotThrow)
{
    appbox::SetLogSink(nullptr);
    appbox::LogEnable(true);

    EXPECT_NO_THROW(appbox::Log(appbox::LOG_LEVEL_INFO, __FILE__, __LINE__, std::string("no sink")));
    EXPECT_EQ(g_received.load(), 0);
}

/**
 * @brief A sink failure is reported by the return value, the log path has to
 *        swallow it and count the dropped message instead of throwing.
 */
TEST(UnitLog, FailingSinkDoesNotThrowAndCountsDrop)
{
    InstallCountingSink(false);
    appbox::LogEnable(true);

    const uint64_t before = appbox::DroppedLogCount();

    EXPECT_NO_THROW(appbox::Log(appbox::LOG_LEVEL_ERROR, __FILE__, __LINE__, std::string("failing sink")));

    EXPECT_EQ(g_received.load(), 1);
    EXPECT_EQ(appbox::DroppedLogCount(), before + 1);

    appbox::SetLogSink(nullptr);
}

/**
 * @brief A successful delivery must not be counted as a dropped message.
 */
TEST(UnitLog, SuccessfulSinkIsNotCountedAsDropped)
{
    InstallCountingSink(true);
    appbox::LogEnable(true);

    const uint64_t before = appbox::DroppedLogCount();

    appbox::Log(appbox::LOG_LEVEL_INFO, __FILE__, __LINE__, std::string("delivered"));

    EXPECT_EQ(g_received.load(), 1);
    EXPECT_EQ(appbox::DroppedLogCount(), before);

    appbox::SetLogSink(nullptr);
}

/**
 * @brief The log switch still suppresses the output when it is disabled.
 */
TEST(UnitLog, LogEnableSuppressesOutput)
{
    InstallCountingSink(true);

    appbox::LogEnable(false);
    appbox::Log(appbox::LOG_LEVEL_INFO, __FILE__, __LINE__, std::string("disabled"));
    EXPECT_EQ(g_received.load(), 0);

    appbox::LogEnable(true);
    appbox::Log(appbox::LOG_LEVEL_INFO, __FILE__, __LINE__, std::string("enabled"));
    EXPECT_EQ(g_received.load(), 1);

    appbox::SetLogSink(nullptr);
}

/**
 * @brief LogGuard suppresses the output while it is alive and restores it
 *        afterwards.
 */
TEST(UnitLog, LogGuardSuppressesOutput)
{
    InstallCountingSink(true);
    appbox::LogEnable(true);

    {
        appbox::LogGuard guard;
        appbox::Log(appbox::LOG_LEVEL_INFO, __FILE__, __LINE__, std::string("guarded"));
    }

    EXPECT_EQ(g_received.load(), 0);

    appbox::Log(appbox::LOG_LEVEL_INFO, __FILE__, __LINE__, std::string("restored"));
    EXPECT_EQ(g_received.load(), 1);

    appbox::SetLogSink(nullptr);
}

/**
 * @brief The buffer of a counted string is not required to be terminated, the
 *        conversion has to read the declared length only.
 */
TEST(UnitLog, UnicodeStringIsReadByLength)
{
    /* No terminator behind the characters. */
    wchar_t       buffer[] = {L'a', L'b', L'c'};
    UNICODE_STRING str = {};
    str.Buffer = buffer;
    str.Length = static_cast<USHORT>(sizeof(buffer));
    str.MaximumLength = static_cast<USHORT>(sizeof(buffer));

    EXPECT_EQ(appbox::UnicodeStringToUTF8(&str), "abc");
}

/**
 * @brief A string which claims more bytes than the caller owns must not be
 *        read. An application can pass such a value on purpose, to find a hook
 *        which follows the buffer blindly.
 */
TEST(UnitLog, UnicodeStringWithInconsistentLengthIsNotRead)
{
    wchar_t        buffer[4] = {L'a', L'b', L'c', L'd'};
    UNICODE_STRING str = {};
    str.Buffer = buffer;
    str.Length = static_cast<USHORT>(8 * sizeof(wchar_t));
    str.MaximumLength = static_cast<USHORT>(sizeof(buffer));

    EXPECT_EQ(appbox::UnicodeStringToUTF8(&str), "");

    const nlohmann::json json = appbox::ToJson(&str);
    EXPECT_EQ(json["Buffer"].get<std::string>(), "");
}

/**
 * @brief A missing buffer or a missing string must be reported instead of
 *        being dereferenced.
 */
TEST(UnitLog, UnicodeStringWithoutBufferIsReported)
{
    EXPECT_EQ(appbox::UnicodeStringToUTF8(nullptr), "");

    UNICODE_STRING str = {};
    EXPECT_EQ(appbox::UnicodeStringToUTF8(&str), "");
    EXPECT_NO_THROW(appbox::ToJson(&str));
    EXPECT_NO_THROW(appbox::ToJson(static_cast<const PUNICODE_STRING>(nullptr)));
}

/**
 * @brief The logger runs inside hooked kernel calls, so a parameter parser
 *        which fails must not let the exception reach the caller.
 */
TEST(UnitLog, LoggerSwallowsParameterFailures)
{
    appbox::LoggerF logger("UnitTest", [](int) -> nlohmann::json { throw std::runtime_error("broken parameter"); });

    EXPECT_NO_THROW(logger.Log(1));
}

/**
 * @brief Logging and toggling the switch from several threads must be race free
 *        and must not crash.
 */
TEST(UnitLog, ConcurrentLogAndToggle)
{
    InstallCountingSink(true);
    appbox::LogEnable(true);

    std::vector<std::thread> threads;
    threads.reserve(kConcurrentThreads);

    for (int t = 0; t < kConcurrentThreads; ++t)
    {
        threads.emplace_back([]() {
            for (int i = 0; i < kConcurrentIterations; ++i)
            {
                appbox::Log(appbox::LOG_LEVEL_TRACE, __FILE__, __LINE__, std::string("concurrent"));
                if ((i % 8) == 0)
                {
                    appbox::LogEnable((i % 16) == 0);
                }
            }
        });
    }

    for (auto& th : threads)
    {
        th.join();
    }

    appbox::LogEnable(true);
    appbox::SetLogSink(nullptr);

    /* Every message is delivered at most once. */
    EXPECT_LE(g_received.load(), kConcurrentThreads * kConcurrentIterations);
}
