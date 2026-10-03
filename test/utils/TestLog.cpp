#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include "TestLog.hpp"

namespace
{

/**
 * @brief The GoogleTest hook which keeps the backtrace around one test case.
 *
 * The buffer is reset when a case starts, so a dump holds the messages of the
 * case which failed only, and it is dumped when that case failed.
 */
class TestLogListener : public testing::EmptyTestEventListener
{
public:
    /**
     * @brief Start a fresh backtrace for the test case which starts.
     * @param[in] test_info The test case which starts.
     */
    void OnTestStart(const testing::TestInfo& /*test_info*/) override
    {
        spdlog::default_logger()->enable_backtrace(appbox::test::kBacktraceMessages);
    }

    /**
     * @brief Dump the captured messages when the test case failed.
     * @param[in] test_info The test case which ended.
     */
    void OnTestEnd(const testing::TestInfo& test_info) override
    {
        if (test_info.result()->Failed())
        {
            appbox::test::DumpTestLog();
        }
    }
};

} // namespace

void appbox::test::InstallTestLog()
{
    /*
     * The level of the default logger decides the live output; the backtrace
     * stores a message whatever its level is, so `off` keeps the run silent
     * without losing the log which a failure has to show.
     */
    spdlog::default_logger()->set_level(spdlog::level::off);
    spdlog::default_logger()->enable_backtrace(kBacktraceMessages);

    testing::UnitTest::GetInstance()->listeners().Append(new TestLogListener());
}

void appbox::test::DumpTestLog()
{
    spdlog::default_logger()->dump_backtrace();
    /* The sink of the default logger buffers, and the run may end here. */
    spdlog::default_logger()->flush();
}
