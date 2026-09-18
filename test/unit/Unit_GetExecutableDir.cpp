#include "utils/GetExecutableDir.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>

/**
 * @brief The configuration path appends the json suffix to the full file name
 *        of the executable, so the .exe part survives.
 */
TEST(GetExecutableDir, DefaultConfigPathKeepsTheExecutableExtension)
{
    EXPECT_EQ(appbox::DefaultConfigPathForExecutable(L"C:\\app\\foo.exe"),
              L"C:\\app\\foo.exe.json");
}

/**
 * @brief An executable without extension still receives the json suffix.
 */
TEST(GetExecutableDir, DefaultConfigPathKeepsAPathWithoutExtension)
{
    EXPECT_EQ(appbox::DefaultConfigPathForExecutable(L"C:\\app\\foo"), L"C:\\app\\foo.json");
}

/**
 * @brief Every dot of the file name is preserved, only the suffix is added.
 */
TEST(GetExecutableDir, DefaultConfigPathKeepsEveryDotOfTheFileName)
{
    EXPECT_EQ(appbox::DefaultConfigPathForExecutable(L"C:\\app\\my.app.exe"),
              L"C:\\app\\my.app.exe.json");
}

/**
 * @brief A relative executable path stays relative.
 */
TEST(GetExecutableDir, DefaultConfigPathKeepsARelativePath)
{
    EXPECT_EQ(appbox::DefaultConfigPathForExecutable(L"foo.exe"), L"foo.exe.json");
}

/**
 * @brief The loader derives its configuration from the name of the running
 *        binary: the file name carries the extension and the configuration
 *        appends the json suffix to it.
 */
TEST(GetExecutableDir, ConfigurationIsNamedAfterTheRunningExecutable)
{
    const auto file_name = appbox::GetExecutableFileName();

    EXPECT_EQ(file_name,
              std::filesystem::path(appbox::GetExecutablePath()).filename().wstring());
    EXPECT_EQ(std::filesystem::path(file_name).extension().wstring(), L".exe");
    EXPECT_EQ(std::filesystem::path(file_name).stem().wstring(), appbox::GetExecutableName());

    const auto config = appbox::DefaultConfigPathForExecutable(appbox::GetExecutablePath());

    EXPECT_EQ(std::filesystem::path(config).filename().wstring(), file_name + L".json");
    EXPECT_EQ(std::filesystem::path(config).parent_path(), std::filesystem::path(appbox::GetExecutableDir()));
}
