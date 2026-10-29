#include "utils/ProcessJob.hpp" /* Must be first include file */
#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Get the path of the image this test executable runs.
 *
 * The cases create a process from the image they run in, so the creation
 * reaches the checks of its parameters instead of failing on a file which does
 * not exist.
 *
 * @return The path of the image, empty when it cannot be read.
 */
std::wstring OwnImagePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return std::wstring();
        }

        if (length < buffer.size())
        {
            return std::wstring(buffer.data(), length);
        }

        buffer.resize(buffer.size() * 2);
    }
}

} // namespace

/**
 * @brief The exit code of a job which was not started is zero.
 */
TEST(Unit_ProcessJob, InitialExitCodeIsZero)
{
    appbox::SandboxConfig cfg;
    appbox::ProcessJob    job(L"appbox_unit_missing.exe", L"appbox_unit_missing.exe", {}, cfg, false, nullptr);

    EXPECT_EQ(job.GetExitCode(), 0u);
}

/**
 * @brief A working directory which does not exist refuses the creation.
 *
 * The directory is handed over as `lpCurrentDirectory` of the creation, which
 * the system checks on the host, so the reported code proves that the
 * parameter of the job reaches the creation instead of being ignored.
 */
TEST(Unit_ProcessJob, MissingCurrentDirectoryIsRejected)
{
    const auto image = OwnImagePath();
    ASSERT_FALSE(image.empty()) << "the image of the test executable cannot be read";

    appbox::SandboxConfig     cfg;
    appbox::ProcessJobOptions options;
    options.current_directory = L"Z:\\appbox_unit_missing_directory";

    appbox::ProcessJob job(image, image, {}, cfg, false, nullptr, options);

    EXPECT_EQ(job.Start(), static_cast<DWORD>(ERROR_DIRECTORY));
}

/**
 * @brief The flags of a creation name the parameters the job was given.
 *
 * The environment block of a creation is UTF-16 and the block of the job is
 * passed as such; a creation which names the job by an attribute carries the
 * extended startup information the attribute list lives in.
 */
TEST(Unit_ProcessJob, CreationFlagsDescribeTheParameters)
{
    EXPECT_EQ(appbox::ProcessCreationFlags(false, false, false), static_cast<DWORD>(CREATE_SUSPENDED));
    EXPECT_EQ(appbox::ProcessCreationFlags(true, false, false),
              static_cast<DWORD>(CREATE_SUSPENDED | CREATE_NO_WINDOW));
    EXPECT_EQ(appbox::ProcessCreationFlags(false, true, false),
              static_cast<DWORD>(CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT));
    EXPECT_EQ(appbox::ProcessCreationFlags(false, false, true),
              static_cast<DWORD>(CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT));
}

/**
 * @brief The options own the environment block of the process.
 *
 * The environment of a request is built from the request of another launcher,
 * and the request is released before the processes of the request are created:
 * an option which only pointed at the block would hand the creation a buffer
 * which is gone by then. The copy of the options keeps the block alive, which
 * is what the case proves after it released the source.
 *
 * A creation which really reads the block is covered by the end to end cases of
 * the multi instance suite: a job of this suite carries no sandbox module, so a
 * creation it starts never runs a program.
 */
TEST(Unit_ProcessJob, OwnsEnvironmentBlock)
{
    /* The block of a process ends with a second null character. */
    const std::wstring text = L"APPBOX_UNIT=1";

    appbox::ProcessJobOptions options;
    {
        std::vector<wchar_t> source(text.begin(), text.end());
        source.push_back(L'\0');
        source.push_back(L'\0');
        options.environment = std::move(source);
    }

    appbox::ProcessJobOptions copy = options;

    /* The source of the block is gone, the copy still carries it. */
    options.environment.clear();
    options.environment.shrink_to_fit();

    ASSERT_FALSE(copy.environment.empty());
    EXPECT_EQ(std::wstring(copy.environment.data()), text);
    EXPECT_EQ(copy.environment.size(), text.size() + 2);
    EXPECT_EQ(copy.environment[text.size()], L'\0');
    EXPECT_EQ(copy.environment[text.size() + 1], L'\0');
}
