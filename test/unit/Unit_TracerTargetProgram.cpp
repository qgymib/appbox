#include <gtest/gtest.h>
#include "tracer/TargetProgram.hpp"
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Path of the running test executable.
 *
 * @return The path of this process image, which exists by definition.
 */
std::filesystem::path ExistingFile()
{
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    const DWORD length =
        ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

} // namespace

/**
 * @brief An existing file is reported as an absolute path, so the debugger and
 *        the report work with one canonical spelling.
 */
TEST(TracerTargetProgram, AnExistingFileBecomesAbsolute)
{
    const std::filesystem::path existing = ExistingFile();
    const std::filesystem::path resolved = appbox::tracer::ResolveTargetProgram(existing);

    EXPECT_FALSE(resolved.empty());
    EXPECT_TRUE(resolved.is_absolute());
    EXPECT_EQ(resolved.filename(), existing.filename());
}

/**
 * @brief A bare name is looked up on the PATH, exactly as CreateProcess does.
 *        cmd.exe is part of every Windows installation.
 */
TEST(TracerTargetProgram, ABareNameIsSearchedOnThePath)
{
    const std::filesystem::path resolved = appbox::tracer::ResolveTargetProgram(L"cmd.exe");

    ASSERT_FALSE(resolved.empty());
    EXPECT_TRUE(resolved.is_absolute());
    EXPECT_EQ(resolved.filename().wstring(), L"cmd.exe");
}

/**
 * @brief A program which does not exist anywhere is reported as not found
 *        instead of being started and failing later.
 */
TEST(TracerTargetProgram, AMissingProgramIsNotFound)
{
    EXPECT_TRUE(appbox::tracer::ResolveTargetProgram(L"appbox-no-such-program.exe").empty());
}

/**
 * @brief A relative path is not searched on the PATH, so a typo stays visible.
 */
TEST(TracerTargetProgram, AMissingRelativePathIsNotFound)
{
    EXPECT_TRUE(
        appbox::tracer::ResolveTargetProgram(L".\\appbox-no-such-program.exe").empty());
}

/**
 * @brief An empty target has nothing to resolve.
 */
TEST(TracerTargetProgram, AnEmptyTargetIsNotFound)
{
    EXPECT_TRUE(appbox::tracer::ResolveTargetProgram({}).empty());
}
