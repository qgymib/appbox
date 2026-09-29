#include "utils/Shell.hpp"
#include "utils/GetExecutableDir.hpp"
#include "utils/CommandLine.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>

/**
 * @brief A command of the shell is handed over through `/c`, which makes the
 *        shell run the command and leave afterwards.
 */
TEST(Unit_Shell, CommandIsRunThroughTheSlashCOption)
{
    const auto args = appbox::BuildShellArguments({ "start", "powershell" });

    EXPECT_EQ(args, std::vector<std::wstring>({ L"/c", L"start", L"powershell" }));
}

/**
 * @brief Without a command the shell is started without any argument, which
 *        makes it run interactively instead of running a command and leaving.
 */
TEST(Unit_Shell, WithoutACommandTheShellRunsInteractively)
{
    EXPECT_TRUE(appbox::BuildShellArguments({}).empty());
}

/**
 * @brief The command of the loader arrives in UTF-8 and is converted to the
 *        wide characters the process creation of Windows expects.
 */
TEST(Unit_Shell, CommandIsConvertedFromUtf8)
{
    /* 中文, spelled out so the case does not depend on the encoding of the source file. */
    const std::string chinese = "\xE4\xB8\xAD\xE6\x96\x87";

    const auto args = appbox::BuildShellArguments({ "echo", chinese });

    ASSERT_EQ(args.size(), 3u);
    EXPECT_EQ(args[0], L"/c");
    EXPECT_EQ(args[1], L"echo");
    EXPECT_EQ(args[2], L"\u4e2d\u6587");
}

/**
 * @brief A `%COMSPEC%` which names an existing file is the shell of the host.
 *
 * The executable of the test run stands in for the command processor: the
 * resolver only has to tell an existing file from a missing one.
 */
TEST(Unit_Shell, ComspecIsUsedWhenItNamesAFile)
{
    const auto comspec = appbox::GetExecutablePath();

    EXPECT_EQ(appbox::ResolveShellPath(comspec, L"Z:\\appbox_unit"), comspec);
}

/**
 * @brief A `%COMSPEC%` which names no file falls back to the command processor
 *        of the system root.
 */
TEST(Unit_Shell, MissingComspecFallsBackToTheSystemRoot)
{
    std::wstring system_root;
    ASSERT_TRUE(appbox::test::ReadEnvironmentVariable(L"SystemRoot", system_root));
    ASSERT_FALSE(system_root.empty());

    const auto shell = appbox::ResolveShellPath(L"Z:\\appbox_unit\\cmd.exe", system_root);

    EXPECT_EQ(std::filesystem::path(shell).lexically_normal(),
              (std::filesystem::path(system_root) / L"System32" / L"cmd.exe").lexically_normal());
}

/**
 * @brief A `%COMSPEC%` which names a folder is refused: the target of the
 *        sandbox has to be a file, so the resolver falls back to the system
 *        root instead of handing a folder to the process creation.
 */
TEST(Unit_Shell, AComspecWhichNamesAFolderIsRefused)
{
    std::wstring system_root;
    ASSERT_TRUE(appbox::test::ReadEnvironmentVariable(L"SystemRoot", system_root));
    ASSERT_FALSE(system_root.empty());

    const auto folder = appbox::GetExecutableDir();
    ASSERT_TRUE(std::filesystem::is_directory(folder));

    const auto shell = appbox::ResolveShellPath(folder, system_root);

    EXPECT_EQ(std::filesystem::path(shell).filename().wstring(), L"cmd.exe");
}

/**
 * @brief A machine which offers neither value leaves the loader without a
 *        shell, which the loader reports instead of starting something.
 */
TEST(Unit_Shell, WithoutAnyUsableValueThereIsNoShell)
{
    EXPECT_TRUE(appbox::ResolveShellPath(L"Z:\\appbox_unit\\cmd.exe", L"Z:\\appbox_unit").empty());
    EXPECT_TRUE(appbox::ResolveShellPath(L"", L"").empty());
}

/**
 * @brief The environment of the machine which runs the case resolves the
 *        command processor of that machine.
 */
TEST(Unit_Shell, TheEnvironmentOfTheMachineResolvesAShell)
{
    const auto shell = appbox::ResolveShellPath();

    ASSERT_FALSE(shell.empty());
    EXPECT_TRUE(std::filesystem::is_regular_file(shell));
    EXPECT_EQ(std::filesystem::path(shell).filename().wstring(), L"cmd.exe");
}
