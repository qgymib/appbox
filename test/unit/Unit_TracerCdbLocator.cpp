#include <gtest/gtest.h>
#include "tracer/CdbLocator.hpp"
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Path of the running test executable.
 *
 * The executable exists by definition, so it is used as the "existing file" of
 * the search order tests instead of creating a temporary file.
 *
 * @return The path of this process image.
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
 * @brief Without candidates nothing can be found, and no exception is thrown.
 */
TEST(TracerCdbLocator, AnEmptyCandidateListFindsNothing)
{
    EXPECT_TRUE(appbox::tracer::FindCdb({}).empty());
}

/**
 * @brief The search returns the first candidate which exists, so the order of
 *        the candidate list decides which debugger is used.
 */
TEST(TracerCdbLocator, TheFirstExistingCandidateWins)
{
    const std::filesystem::path existing = ExistingFile();
    const std::filesystem::path missing =
        std::filesystem::path(L"Z:\\appbox\\no\\such\\directory") / appbox::tracer::kCdbFileName;

    EXPECT_EQ(appbox::tracer::FindCdb({missing, existing}), existing);
    EXPECT_EQ(appbox::tracer::FindCdb({existing, missing}), existing);
    EXPECT_TRUE(appbox::tracer::FindCdb({missing}).empty());
}

/**
 * @brief An explicit path is used as given, without searching.
 */
TEST(TracerCdbLocator, AnExplicitExistingPathIsUsed)
{
    const std::filesystem::path existing = ExistingFile();

    EXPECT_EQ(appbox::tracer::ResolveCdb(existing), existing);
}

/**
 * @brief An explicit path which does not exist is reported as "not found"
 *        instead of silently falling back to the search, so a typo in --cdb is
 *        visible to the user.
 */
TEST(TracerCdbLocator, AMissingExplicitPathIsNotFound)
{
    EXPECT_TRUE(appbox::tracer::ResolveCdb(L"Z:\\appbox\\no\\such\\directory\\cdb.exe").empty());
}

/**
 * @brief The default search list always has entries: the debugger of the
 *        Windows SDK is installed below the Windows Kits directory, and a
 *        debugger on the PATH is covered as well.
 */
TEST(TracerCdbLocator, DefaultCandidatesAreNotEmpty)
{
    EXPECT_FALSE(appbox::tracer::DefaultCdbCandidates().empty());
}

/**
 * @brief Every default candidate names cdb.exe, so the search can never pick an
 *        unrelated executable.
 */
TEST(TracerCdbLocator, DefaultCandidatesAreCdbExecutables)
{
    for (const auto& candidate : appbox::tracer::DefaultCdbCandidates())
    {
        EXPECT_EQ(candidate.filename().wstring(), appbox::tracer::kCdbFileName);
    }
}
