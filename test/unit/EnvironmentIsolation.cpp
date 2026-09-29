#include <gtest/gtest.h>
#include "common/EnvironmentIsolation.hpp"
#include <string>

namespace
{

using appbox::EnvironmentIsolation;
using appbox::EnvironmentMergeMode;
using appbox::environment_isolation::ComposedValue;
using appbox::environment_isolation::ComposeEnvironmentValue;

/**
 * @brief Compose a value with a host value which is present.
 * @param[in] host_value Value of the host.
 * @param[in] value Value the user entered in the workspace.
 * @param[in] isolation Isolation mode of the variable.
 * @param[in] merge Merge mode of the variable.
 * @return The composed value.
 */
ComposedValue Compose(const std::wstring& host_value, const std::wstring& value, EnvironmentIsolation isolation,
                      EnvironmentMergeMode merge)
{
    return ComposeEnvironmentValue(true, host_value, value, isolation, merge, L";");
}

} // namespace

TEST(Unit_EnvironmentIsolation, FullReportsTheStoredValueForEveryMergeMode)
{
    /* The value of the host is invisible, so the merge mode cannot contribute. */
    for (const auto merge : { EnvironmentMergeMode::Replace, EnvironmentMergeMode::Host, EnvironmentMergeMode::Prepend,
                              EnvironmentMergeMode::Append })
    {
        const ComposedValue composed = Compose(L"foo", L"bar", EnvironmentIsolation::Full, merge);
        EXPECT_TRUE(composed.visible);
        EXPECT_EQ(composed.value, L"bar");
    }
}

TEST(Unit_EnvironmentIsolation, WriteCopyReplaceReportsTheStoredValue)
{
    const ComposedValue composed =
        Compose(L"foo", L"bar", EnvironmentIsolation::WriteCopy, EnvironmentMergeMode::Replace);
    EXPECT_TRUE(composed.visible);
    EXPECT_EQ(composed.value, L"bar");
}

TEST(Unit_EnvironmentIsolation, WriteCopyHostReportsTheHostValue)
{
    const ComposedValue composed = Compose(L"foo", L"bar", EnvironmentIsolation::WriteCopy, EnvironmentMergeMode::Host);
    EXPECT_TRUE(composed.visible);
    EXPECT_EQ(composed.value, L"foo");
}

TEST(Unit_EnvironmentIsolation, WriteCopyHostWithoutAHostValueHidesTheVariable)
{
    /*
     * The mode ignores the stored value, so a variable the host does not hold
     * has nothing to report and is absent instead of being present and empty.
     */
    const ComposedValue composed =
        ComposeEnvironmentValue(false, L"", L"bar", EnvironmentIsolation::WriteCopy, EnvironmentMergeMode::Host, L";");
    EXPECT_FALSE(composed.visible);
}

TEST(Unit_EnvironmentIsolation, WriteCopyPrependJoinsTheStoredValueInFront)
{
    const ComposedValue composed =
        Compose(L"foo", L"bar", EnvironmentIsolation::WriteCopy, EnvironmentMergeMode::Prepend);
    EXPECT_TRUE(composed.visible);
    EXPECT_EQ(composed.value, L"bar;foo");
}

TEST(Unit_EnvironmentIsolation, WriteCopyAppendJoinsTheStoredValueBehind)
{
    const ComposedValue composed =
        Compose(L"foo", L"bar", EnvironmentIsolation::WriteCopy, EnvironmentMergeMode::Append);
    EXPECT_TRUE(composed.visible);
    EXPECT_EQ(composed.value, L"foo;bar");
}

TEST(Unit_EnvironmentIsolation, JoiningWithoutAHostValueKeepsTheStoredValueAlone)
{
    /* The merge string joins two values and is not written while there is one. */
    for (const auto merge : { EnvironmentMergeMode::Prepend, EnvironmentMergeMode::Append })
    {
        const ComposedValue composed =
            ComposeEnvironmentValue(false, L"", L"bar", EnvironmentIsolation::WriteCopy, merge, L";");
        EXPECT_TRUE(composed.visible);
        EXPECT_EQ(composed.value, L"bar");
    }
}

TEST(Unit_EnvironmentIsolation, JoiningWithAnEmptyHostValueKeepsTheStoredValueAlone)
{
    /*
     * An empty entry of a list like the search path names the current
     * directory, so a separator which joins nothing is not written either.
     */
    for (const auto merge : { EnvironmentMergeMode::Prepend, EnvironmentMergeMode::Append })
    {
        const ComposedValue composed = Compose(L"", L"bar", EnvironmentIsolation::WriteCopy, merge);
        EXPECT_TRUE(composed.visible);
        EXPECT_EQ(composed.value, L"bar");
    }
}

TEST(Unit_EnvironmentIsolation, AnEmptyStoredValueIsAVisibleVariable)
{
    /* Clearing a variable is not the same as removing it from the environment. */
    const ComposedValue composed = Compose(L"foo", L"", EnvironmentIsolation::WriteCopy, EnvironmentMergeMode::Replace);
    EXPECT_TRUE(composed.visible);
    EXPECT_EQ(composed.value, L"");
}

TEST(Unit_EnvironmentIsolation, OnlyWriteCopyLetsTheHostValueBeVisible)
{
    EXPECT_TRUE(appbox::environment_isolation::HostValueIsVisible(EnvironmentIsolation::WriteCopy));
    EXPECT_FALSE(appbox::environment_isolation::HostValueIsVisible(EnvironmentIsolation::Full));
}

TEST(Unit_EnvironmentIsolation, IsolationTokensRoundTrip)
{
    for (const auto isolation : { EnvironmentIsolation::Full, EnvironmentIsolation::WriteCopy })
    {
        EnvironmentIsolation parsed = EnvironmentIsolation::Full;
        ASSERT_TRUE(appbox::environment_isolation::ParseIsolationToken(
            appbox::environment_isolation::IsolationToken(isolation), parsed));
        EXPECT_EQ(parsed, isolation);
    }
}

TEST(Unit_EnvironmentIsolation, MergeModeTokensRoundTrip)
{
    for (const auto merge : { EnvironmentMergeMode::Replace, EnvironmentMergeMode::Host, EnvironmentMergeMode::Prepend,
                              EnvironmentMergeMode::Append })
    {
        EnvironmentMergeMode parsed = EnvironmentMergeMode::Replace;
        ASSERT_TRUE(appbox::environment_isolation::ParseMergeModeToken(
            appbox::environment_isolation::MergeModeToken(merge), parsed));
        EXPECT_EQ(parsed, merge);
    }
}

TEST(Unit_EnvironmentIsolation, TokensAreReadIgnoringTheCaseAndTheSeparator)
{
    /* The display names of the workspace are accepted as well. */
    EnvironmentIsolation isolation = EnvironmentIsolation::Full;
    EXPECT_TRUE(appbox::environment_isolation::ParseIsolationToken("Write Copy", isolation));
    EXPECT_EQ(isolation, EnvironmentIsolation::WriteCopy);
    EXPECT_TRUE(appbox::environment_isolation::ParseIsolationToken("WRITE-COPY", isolation));
    EXPECT_EQ(isolation, EnvironmentIsolation::WriteCopy);
    EXPECT_TRUE(appbox::environment_isolation::ParseIsolationToken("writecopy", isolation));
    EXPECT_EQ(isolation, EnvironmentIsolation::WriteCopy);

    EnvironmentMergeMode merge = EnvironmentMergeMode::Replace;
    EXPECT_TRUE(appbox::environment_isolation::ParseMergeModeToken("PREPEND", merge));
    EXPECT_EQ(merge, EnvironmentMergeMode::Prepend);
    EXPECT_TRUE(appbox::environment_isolation::ParseMergeModeToken("append", merge));
    EXPECT_EQ(merge, EnvironmentMergeMode::Append);

    EXPECT_FALSE(appbox::environment_isolation::ParseIsolationToken("merge", isolation));
    EXPECT_FALSE(appbox::environment_isolation::ParseMergeModeToken("whiteout", merge));
}

TEST(Unit_EnvironmentIsolation, IsolationFileSchemaIsStable)
{
    /* The wire format is a contract between the packer and the sandbox. */
    using namespace appbox::environment_isolation;

    EXPECT_EQ(kVersion, 1);
    EXPECT_STREQ(kVersionKey, "version");
    EXPECT_STREQ(kEntriesKey, "entries");
    EXPECT_STREQ(kNameKey, "name");
    EXPECT_STREQ(kValueKey, "value");
    EXPECT_STREQ(kIsolationKey, "isolation");
    EXPECT_STREQ(kMergeKey, "merge");
    EXPECT_STREQ(kMergeStringKey, "merge_string");
}

TEST(Unit_EnvironmentIsolation, StateFileSchemaIsStable)
{
    using namespace appbox::environment_isolation;

    EXPECT_EQ(kStateVersion, 1);
    EXPECT_STREQ(kStateVersionKey, "version");
    EXPECT_STREQ(kStateEntriesKey, "entries");
    EXPECT_STREQ(kStateNameKey, "name");
    EXPECT_STREQ(kStateValueKey, "value");
    EXPECT_STREQ(kStateDeletedKey, "deleted");
}
