#include <gtest/gtest.h>
#include "src/core/AboutInfo.hpp"
#include <cstddef>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Names of the libraries the About dialog lists, in the order it shows
 *        them.
 *
 * The order is the alphabetical one of the dialog, so a change of the list or
 * of its order is a deliberate change of the dialog and not an accident.
 */
const std::vector<std::string> kExpectedDependencies = {
    "asio", "Detours", "expected", "libzip", "nlohmann_json", "spdlog", "wxWidgets", "zlib"};

/**
 * @brief Check whether a text is matched by a regular expression as a whole.
 * @param[in] text Text to check.
 * @param[in] pattern Pattern the whole text has to match.
 * @return true when the text matches the pattern.
 */
bool Matches(const std::string& text, const char* pattern)
{
    return std::regex_match(text, std::regex(pattern));
}

} // namespace

/**
 * @brief The functional description is a single short sentence, so the dialog
 *        no longer opens with several paragraphs of text.
 */
TEST(UnitAboutInfo, SummaryIsOneShortSentence)
{
    const std::string summary = appbox::kAboutSummary;

    EXPECT_FALSE(summary.empty());
    EXPECT_EQ(summary.find('\n'), std::string::npos);
    EXPECT_EQ(summary.find('\r'), std::string::npos);
    EXPECT_EQ(summary.back(), '.');
    EXPECT_LT(summary.size(), std::size_t{100});
}

/**
 * @brief The version is the dotted number the CMake project declares.
 */
TEST(UnitAboutInfo, VersionIsADottedNumber)
{
    EXPECT_TRUE(Matches(appbox::GetAboutInfo().version, "[0-9]+\\.[0-9]+\\.[0-9]+"));
}

/**
 * @brief The build date is the local time of the build.
 */
TEST(UnitAboutInfo, BuildDateIsATimestamp)
{
    EXPECT_TRUE(Matches(appbox::GetAboutInfo().build_date,
                        "[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}"));
}

/**
 * @brief The git fields are always filled: a build without git degrades them to
 *        `unknown` instead of leaving them empty.
 */
TEST(UnitAboutInfo, GitFieldsAreAlwaysFilled)
{
    const auto& info = appbox::GetAboutInfo();

    EXPECT_FALSE(info.git_revision.empty());
    EXPECT_FALSE(info.git_branch.empty());

    /* Both fields come from the same query, so they degrade together. */
    EXPECT_EQ(info.git_revision == "unknown", info.git_branch == "unknown");

    if (info.git_revision != "unknown")
    {
        EXPECT_TRUE(Matches(info.git_revision, "[0-9a-f]{7,40}"));
    }
}

/**
 * @brief The dialog lists exactly the linked third-party libraries, ordered by
 *        name, and every entry carries a version.
 */
TEST(UnitAboutInfo, DependenciesAreTheLinkedLibraries)
{
    const auto& info = appbox::GetAboutInfo();

    ASSERT_EQ(info.dependencies.size(), kExpectedDependencies.size());

    std::set<std::string> seen;
    for (std::size_t index = 0; index < info.dependencies.size(); ++index)
    {
        const auto& dependency = info.dependencies[index];

        EXPECT_EQ(dependency.name, kExpectedDependencies[index]);
        EXPECT_TRUE(seen.insert(dependency.name).second)
            << "duplicate dependency: " << dependency.name;
        EXPECT_TRUE(Matches(dependency.version, "[0-9]+\\.[0-9]+(\\.[0-9]+)?"))
            << dependency.name << " has the unexpected version " << dependency.version;
    }
}

/**
 * @brief The information is a constant of the binary, so every call hands out
 *        the same instance instead of reading the values again.
 */
TEST(UnitAboutInfo, InformationIsStableAcrossCalls)
{
    EXPECT_EQ(&appbox::GetAboutInfo(), &appbox::GetAboutInfo());
}
