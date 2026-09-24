#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "utils/CommandLine.hpp"
#include "utils/Coredump.hpp"
#include "utils/TestTimeout.hpp"

/**
 * @brief A name of a test case is a name a file can hold.
 */
TEST(TestTimeout, TagIsSanitized)
{
    EXPECT_EQ(appbox::test::SanitizeCoredumpTag(L"Reg.WriteValue_NewKey"), L"Reg.WriteValue_NewKey");
    EXPECT_EQ(appbox::test::SanitizeCoredumpTag(L"a b/c:d\\e"), L"a_b_c_d_e");
    EXPECT_EQ(appbox::test::SanitizeCoredumpTag(L""), L"test");
}

/**
 * @brief An option of the command line is found by its name only.
 */
TEST(TestTimeout, CommandLineOptionIsFound)
{
    const std::wstring command_line = L"\"C:\\Program Files\\AppBox\\AppBoxUnitTests.exe\" "
                                      L"--loader=C:\\x\\AppBoxLoader.exe --test-timeout=7 --gtest_filter=A.*";

    std::wstring value;
    EXPECT_TRUE(appbox::test::FindCommandLineOption(command_line, L"test-timeout", value));
    EXPECT_EQ(value, L"7");

    EXPECT_TRUE(appbox::test::FindCommandLineOption(command_line, L"loader", value));
    EXPECT_EQ(value, L"C:\\x\\AppBoxLoader.exe");

    /* A name which is only a part of another option is not a match. */
    EXPECT_FALSE(appbox::test::FindCommandLineOption(command_line, L"timeout", value));
    EXPECT_TRUE(value.empty());

    /* A value with a space is quoted by the builder of the command line. */
    EXPECT_TRUE(appbox::test::FindCommandLineOption(L"AppBoxUnitTests.exe --test-dump-dir=\"C:\\my dir\\dumps\"",
                                                   L"test-dump-dir", value));
    EXPECT_EQ(value, L"C:\\my dir\\dumps");

    /* An option has to start a token. */
    EXPECT_FALSE(appbox::test::FindCommandLineOption(L"AppBoxUnitTests.exe --x--test-timeout=9", L"test-timeout",
                                                     value));
}

/**
 * @brief The environment overrides the timeout and the directory of the dumps.
 */
TEST(TestTimeout, EnvironmentIsRead)
{
    ::SetEnvironmentVariableW(appbox::test::kTestTimeoutEnv, L"42");
    ::SetEnvironmentVariableW(appbox::test::kTestDumpDirEnv, L"C:\\dumps");

    appbox::test::TestTimeoutConfig config;
    appbox::test::LoadTestTimeoutFromEnvironment(config);

    EXPECT_EQ(config.test_timeout_seconds, 42);
    EXPECT_EQ(config.test_dump_dir, L"C:\\dumps");

    /* A value which is not a timeout keeps the default. */
    ::SetEnvironmentVariableW(appbox::test::kTestTimeoutEnv, L"abc");

    appbox::test::TestTimeoutConfig other;
    appbox::test::LoadTestTimeoutFromEnvironment(other);

    EXPECT_EQ(other.test_timeout_seconds, appbox::test::kDefaultTestTimeoutSeconds);

    ::SetEnvironmentVariableW(appbox::test::kTestTimeoutEnv, nullptr);
    ::SetEnvironmentVariableW(appbox::test::kTestDumpDirEnv, nullptr);
}

/**
 * @brief The directory of the dumps defaults to a directory below the test
 *        executable.
 */
TEST(TestTimeout, TheDefaultDumpDirectoryIsBelowTheExecutable)
{
    appbox::test::TestTimeoutConfig config;

    const std::filesystem::path expected =
        std::filesystem::path(appbox::test::GetOwnExecutablePath()).parent_path() / L"coredump";
    EXPECT_EQ(appbox::test::ResolveTestDumpDir(config), expected);

    config.test_dump_dir = L"C:\\dumps";
    EXPECT_EQ(appbox::test::ResolveTestDumpDir(config), std::filesystem::path(L"C:\\dumps"));
}

/**
 * @brief The request of the coredump writer is read back from the command line
 *        it was built for.
 */
TEST(TestTimeout, TheWriterCommandLineIsReadBack)
{
    appbox::test::CoredumpRequest request;
    request.pid = 4711;
    request.dump_dir = std::filesystem::path(L"C:\\tmp\\dump dir");
    request.tag = L"Suite.Case";

    const std::wstring command_line = appbox::test::BuildCoredumpWriterCommandLine(request);

    appbox::test::CoredumpRequest parsed;
    ASSERT_TRUE(appbox::test::ParseCoredumpRequest(command_line, parsed));

    EXPECT_EQ(parsed.pid, request.pid);
    EXPECT_EQ(parsed.dump_dir, request.dump_dir);
    EXPECT_EQ(parsed.tag, request.tag);
}

/**
 * @brief A command line which does not ask for a writer, and a request which is
 *        not complete, are refused.
 */
TEST(TestTimeout, AnIncompleteWriterRequestIsRefused)
{
    appbox::test::CoredumpRequest request;

    EXPECT_FALSE(appbox::test::ParseCoredumpRequest(L"\"C:\\x\\AppBoxTests.exe\" --test-timeout=5", request));
    EXPECT_FALSE(appbox::test::ParseCoredumpRequest(L"AppBoxTests.exe --appbox-coredump-writer=1", request));
    EXPECT_FALSE(appbox::test::ParseCoredumpRequest(
        L"AppBoxTests.exe --appbox-coredump-writer=1 --appbox-coredump-pid=42", request));
    EXPECT_FALSE(appbox::test::ParseCoredumpRequest(
        L"AppBoxTests.exe --appbox-coredump-writer=1 --appbox-coredump-pid=0 --appbox-coredump-dir=C:\\dumps",
        request));
    EXPECT_FALSE(appbox::test::ParseCoredumpRequest(
        L"AppBoxTests.exe --appbox-coredump-writer=1 --appbox-coredump-pid=42 --appbox-coredump-dir=", request));
}
