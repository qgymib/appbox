#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "utils/ProbeCall.hpp"
#include "utils/TestLog.hpp"

namespace
{

/**
 * @brief A directory of the machine which is removed when the case ends.
 */
class ScratchDirectory
{
public:
    /**
     * @brief Create the directory below the temporary directory of the machine.
     */
    ScratchDirectory()
    {
        static unsigned counter = 0;
        ++counter;

        path_ = std::filesystem::temp_directory_path() /
                ("appbox-test-log-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(counter));
        std::filesystem::create_directories(path_);
    }

    ~ScratchDirectory()
    {
        std::error_code code;
        std::filesystem::remove_all(path_, code);
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    /**
     * @brief The path of the directory.
     * @return The path of the directory.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

private:
    /** The path of the directory. */
    std::filesystem::path path_;
};

/**
 * @brief The text of a file, empty when it cannot be read.
 * @param[in] path Path of the file.
 * @return The text of the file.
 */
std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        return {};
    }

    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

} // namespace

/**
 * Condition:
 * 1. A case names the working directory of its run, which is what the fixture
 *    of the cases does, and it logs a message while it runs.
 * 2. The run of the case failed, which is what makes the harness dump the
 *    program log it captured.
 *
 * Expected:
 * 1. The dump is written into the `log` directory of the working directory of
 *    the case, so the directory of a case which failed holds a file and is
 *    therefore part of the artifact of the run.
 * 2. The file holds the messages of the case, with the markers of the dump.
 */
TEST(Unit_TestLog, WritesTheLogOfTheCaseIntoItsWorkingDirectory)
{
    const ScratchDirectory directory;
    appbox::test::SetCaseLogDirectory(directory.Get());

    SPDLOG_DEBUG("a message of the case which failed");
    appbox::test::DumpTestLog();

    const auto log_directory = directory.Get() / appbox::test::kCaseLogDirName;
    ASSERT_TRUE(std::filesystem::exists(log_directory));

    std::size_t files = 0;
    std::string text;
    for (const auto& entry : std::filesystem::directory_iterator(log_directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        ++files;
        text += ReadText(entry.path());
    }

    EXPECT_EQ(files, 1u);
    EXPECT_NE(text.find("a message of the case which failed"), std::string::npos);
    EXPECT_NE(text.find("Backtrace Start"), std::string::npos);

    appbox::test::SetCaseLogDirectory({});
}
