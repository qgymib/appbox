#include "utils/WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "utils/CrashReport.hpp"
#include "utils/LogFile.hpp"

namespace
{

/**
 * @brief Create an empty directory below the temporary directory of the machine.
 *
 * The name does not carry the process id, because a death test runs its test
 * body in a second process of the same executable and both processes have to
 * agree on the directory of the test.
 *
 * @param[in] name Name which tells the directories of the tests apart.
 * @return The path of the directory, empty when it cannot be created.
 */
std::filesystem::path MakeTempDir(const std::string& name)
{
    std::error_code ec;
    const auto      base = std::filesystem::temp_directory_path(ec);
    if (ec)
    {
        return {};
    }

    const auto dir = base / ("appbox-unit-" + name);

    std::filesystem::remove_all(dir, ec);
    ec.clear();
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        return {};
    }

    return dir;
}

/**
 * @brief Read every file of a directory.
 *
 * The name of a log file carries the process id of the process which wrote it,
 * which a test does not know when the process is a child of the test run.
 *
 * @param[in] dir Directory to read.
 * @return The concatenated content of its files.
 */
std::string ReadFiles(const std::filesystem::path& dir)
{
    std::string     text;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream.is_open())
        {
            continue;
        }

        text.append(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    return text;
}

/**
 * @brief Raise the access violation a report of a crash has to name.
 *
 * The statement of a death test cannot hold a comma of its own, so the call
 * which carries the parameters of the exception is wrapped in a function.
 */
void RaiseAccessViolation()
{
    const ULONG_PTR arguments[2] = { 1, 0x1234 };
    ::RaiseException(0xC0000005, 0, 2, arguments);
}

} // namespace

/**
 * @brief The name of a log file names the program, the UTC time the process
 *        started at and the process id, so two processes of one run never write
 *        the same file.
 */
TEST(Unit_LogFile, FileNameNamesTheProgramTheTimeAndTheProcess)
{
    SYSTEMTIME utc = {};
    utc.wYear = 2026;
    utc.wMonth = 9;
    utc.wDay = 30;
    utc.wHour = 0;
    utc.wMinute = 20;
    utc.wSecond = 19;

    EXPECT_EQ(appbox::LogFile::FileNameOf(L"C:\\a\\b\\AppBoxTests.exe", utc, 5576),
              L"AppBoxTests.20260930T002019Z.5576.log");

    /* The extension of a program is dropped whatever its case. */
    EXPECT_EQ(appbox::LogFile::FileNameOf(L"C:\\a\\AppBoxLauncher.EXE", utc, 1),
              L"AppBoxLauncher.20260930T002019Z.1.log");

    /* A program without an extension keeps its name. */
    EXPECT_EQ(appbox::LogFile::FileNameOf(L"tool", utc, 2), L"tool.20260930T002019Z.2.log");

    /* A path which names no program still produces a usable file name. */
    EXPECT_EQ(appbox::LogFile::FileNameOf(L"", utc, 3), L"process.20260930T002019Z.3.log");
    EXPECT_EQ(appbox::LogFile::FileNameOf(L"C:\\a\\", utc, 4), L"process.20260930T002019Z.4.log");
}

/**
 * @brief The log file is created in the directory of the run, the lines are
 *        appended and the file is closed again.
 */
TEST(Unit_LogFile, WriteAppendsTheLinesOfTheProcess)
{
    const auto dir = MakeTempDir("logfile");
    ASSERT_FALSE(dir.empty()) << "the temporary directory cannot be created";

    ASSERT_TRUE(appbox::LogFile::Open(dir.wstring(), L"C:\\a\\AppBoxTests.exe"));
    ASSERT_TRUE(appbox::LogFile::IsOpen());
    EXPECT_EQ(std::filesystem::path(appbox::LogFile::Path()).parent_path(), dir);

    appbox::LogFile::WriteLine("first");
    appbox::LogFile::Write("second", 6);
    appbox::LogFile::Close();

    EXPECT_FALSE(appbox::LogFile::IsOpen());

    const std::string text = ReadFiles(dir);
    EXPECT_NE(text.find("first\r\n"), std::string::npos) << text;
    EXPECT_NE(text.find("second"), std::string::npos) << text;

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

/**
 * @brief A directory which does not exist is created for the log file, so a run
 *        whose directory was removed while it was starting still reports.
 */
TEST(Unit_LogFile, OpenCreatesTheDirectoriesOfThePath)
{
    const auto base = MakeTempDir("logdir");
    ASSERT_FALSE(base.empty()) << "the temporary directory cannot be created";

    const auto dir = base / "nested" / "logs";

    ASSERT_TRUE(appbox::LogFile::Open(dir.wstring(), L"C:\\a\\AppBoxTests.exe"));
    appbox::LogFile::WriteLine("nested");
    appbox::LogFile::Close();

    EXPECT_NE(ReadFiles(dir).find("nested"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

/**
 * @brief A fatal exception is reported in the log file of the process which
 *        raised it: the code of the exception, its parameters and the stack of
 *        the process.
 *
 * The statement of the death test runs in the process of the death test, which
 * is a second run of this executable: the log file is opened there and the
 * crash is the crash of that process. The report is the only record of it,
 * because the process dies before anything else can look at it.
 */
TEST(Unit_LogFile, CrashReportNamesTheExceptionOfTheProcess)
{
    const auto dir = MakeTempDir("crash");
    ASSERT_FALSE(dir.empty()) << "the temporary directory cannot be created";

    EXPECT_DEATH(
        {
            appbox::InstallCrashHandler();
            appbox::LogFile::Open(dir.wstring(), L"C:\\a\\AppBoxTests.exe");
            RaiseAccessViolation();
        },
        "");

    const std::string text = ReadFiles(dir);

    EXPECT_NE(text.find("=== crash ==="), std::string::npos) << text;
    EXPECT_NE(text.find("code=0xc0000005"), std::string::npos) << text;
    EXPECT_NE(text.find("params=0x1,0x1234"), std::string::npos) << text;
    EXPECT_NE(text.find("stack="), std::string::npos) << text;
    EXPECT_NE(text.find("=== end crash ==="), std::string::npos) << text;

    appbox::LogFile::Close();

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
