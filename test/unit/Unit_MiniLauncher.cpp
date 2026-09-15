#include "utils/MiniLauncher.hpp" /* Must be first include file */
#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <vector>

/**
 * @brief A target which cannot be created is reported as an error code instead
 *        of an uninitialized exit code.
 */
TEST(UnitMiniLauncher, CreateFailureReturnsErrorCode)
{
    const DWORD ret = appbox::MiniLauncer(L"Z:\\appbox_unit\\does_not_exist.exe", {});

    EXPECT_NE(ret, 0u);
    EXPECT_TRUE(ret == ERROR_FILE_NOT_FOUND || ret == ERROR_PATH_NOT_FOUND)
        << "unexpected error code: " << ret;
}

/**
 * @brief The exit code of the child process is passed through.
 */
TEST(UnitMiniLauncher, ReturnsChildExitCode)
{
    wchar_t     comspec[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH);

    ASSERT_GT(length, 0u);
    ASSERT_LT(length, MAX_PATH);

    const DWORD ret = appbox::MiniLauncer(comspec, { L"/c", L"exit", L"7" });

    EXPECT_EQ(ret, 7u);
}

/**
 * @brief A child process which exits successfully reports a zero exit code.
 */
TEST(UnitMiniLauncher, ReturnsZeroForSuccessfulChild)
{
    wchar_t     comspec[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH);

    ASSERT_GT(length, 0u);
    ASSERT_LT(length, MAX_PATH);

    const DWORD ret = appbox::MiniLauncer(comspec, { L"/c", L"exit", L"0" });

    EXPECT_EQ(ret, 0u);
}
