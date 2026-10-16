#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <gtest/gtest.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <memory>
#include <string>
#include "common/LogFileName.hpp"
#include "utils/ProbeCall.hpp"
#include "TestLog.hpp"

namespace
{

/** The working directory of the running test case, empty when none was named. */
std::filesystem::path g_case_directory;

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

/**
 * @brief A sink which collects the text of the messages it is given.
 *
 * The dump of a case is written into the file of the case with the text the
 * console shows, so the messages are formatted the way the console formats
 * them and the text is written with the path of the case: a directory of the
 * machine whose name carries characters outside the code page of the process
 * stays readable.
 */
class CollectingSink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    /**
     * @brief The text which was collected.
     * @return The text of the messages, in the order they arrived.
     */
    const std::string& Text() const
    {
        return text_;
    }

protected:
    /**
     * @brief Collect the text of one message.
     * @param[in] message The message to collect.
     */
    void sink_it_(const spdlog::details::log_msg& message) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(message, formatted);
        text_.append(formatted.data(), formatted.size());
    }

    /**
     * @brief Flush the sink, which holds its text in memory.
     */
    void flush_() override
    {
    }

private:
    /** The text of the messages which arrived. */
    std::string text_;
};

/**
 * @brief The path of the log file the dump of the running case is written to.
 *
 * The name follows the rule of the launcher and of the sandbox, so the log of
 * the test run is recognized by the same pattern as the logs of the processes a
 * case started, and a run never overwrites the log of an earlier one.
 *
 * @return The path of the log file, empty when no case named its directory.
 */
std::filesystem::path CaseLogPath()
{
    if (g_case_directory.empty())
    {
        return {};
    }

    wchar_t image[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, image, MAX_PATH) == 0)
    {
        return {};
    }

    SYSTEMTIME utc = {};
    ::GetSystemTime(&utc);

    appbox::LogFileTimestamp timestamp;
    timestamp.year = utc.wYear;
    timestamp.month = utc.wMonth;
    timestamp.day = utc.wDay;
    timestamp.hour = utc.wHour;
    timestamp.minute = utc.wMinute;
    timestamp.second = utc.wSecond;

    const auto name = appbox::LogFileNameOf(image, timestamp, ::GetCurrentProcessId());
    return g_case_directory / appbox::test::kCaseLogDirName / name;
}

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
    const auto path = CaseLogPath();
    if (path.empty())
    {
        spdlog::default_logger()->dump_backtrace();
        /* The sink of the default logger buffers, and the run may end here. */
        spdlog::default_logger()->flush();
        return;
    }

    /*
     * The dump is taken with a sink of its own, so the text of the messages is
     * written into the file of the case as well as to the console: the log of
     * the run of a case which failed is what a reader of the artifact opens
     * first, and the directory of the case carries it.
     */
    auto collected = std::make_shared<CollectingSink>();
    spdlog::default_logger()->sinks().push_back(collected);
    spdlog::default_logger()->dump_backtrace();
    spdlog::default_logger()->sinks().pop_back();
    spdlog::default_logger()->flush();

    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return;
    }

    const auto& text = collected->Text();
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void appbox::test::SetCaseLogDirectory(const std::filesystem::path& directory)
{
    g_case_directory = directory;
}
