#include <gtest/gtest.h>
#include <cstddef>
#include <string>
#include <string_view>
#include "LogLevelName.hpp"

namespace
{

/**
 * @brief One expectation of the vocabulary: a level and the text it is spelled
 *        with.
 */
struct TokenCase
{
    appbox::LogLevel level; /* Level. */
    const wchar_t*   token; /* Text of the level, as a command line spells it. */
};

/**
 * @brief Every level of the run and its token.
 *
 * The text of `LogLevel::Error` is `err` and not `error`, because it is the
 * spelling the option of the launcher and the log file of the sandbox accept.
 */
constexpr TokenCase kTokens[] = {
    { appbox::LogLevel::Trace,    L"trace"    },
    { appbox::LogLevel::Debug,    L"debug"    },
    { appbox::LogLevel::Info,     L"info"     },
    { appbox::LogLevel::Warn,     L"warn"     },
    { appbox::LogLevel::Error,    L"err"      },
    { appbox::LogLevel::Critical, L"critical" },
    { appbox::LogLevel::Off,      L"off"      },
};

/** Number of the levels above. */
constexpr std::size_t kTokenCount = sizeof(kTokens) / sizeof(kTokens[0]);

/**
 * @brief Turn a wide text into a narrow one, for the message of a case.
 * @param[in] text Text to convert, ASCII only.
 * @return The text as UTF-8, which is the same bytes as ASCII.
 */
std::string Narrow(const std::wstring& text)
{
    std::string narrow;
    narrow.reserve(text.size());
    for (const wchar_t character : text)
    {
        narrow.push_back(character < 0x80 ? static_cast<char>(character) : '?');
    }

    return narrow;
}

/**
 * @brief Build the text of a token with every character folded to upper case.
 * @param[in] token Token to fold.
 * @return The upper case text.
 */
std::wstring UpperCase(const wchar_t* token)
{
    std::wstring text(token);
    for (auto& character : text)
    {
        if (character >= L'a' && character <= L'z')
        {
            character = static_cast<wchar_t>(character - L'a' + L'A');
        }
    }

    return text;
}

/**
 * @brief Build the text of a token with the case of every character swapped.
 * @param[in] token Token to fold.
 * @return The mixed case text.
 */
std::wstring SwappedCase(const wchar_t* token)
{
    std::wstring text(token);
    for (auto& character : text)
    {
        if (character >= L'a' && character <= L'z')
        {
            character = static_cast<wchar_t>(character - L'a' + L'A');
        }
        else if (character >= L'A' && character <= L'Z')
        {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
    }

    return text;
}

/**
 * @brief Check that a name is no level and reports `kInvalidArgument`.
 *
 * The two expectations are made at the call site instead of inside a helper,
 * because an assertion which fails inside a helper reports the line of the
 * helper and not the name which was read.
 *
 * @param[in] name Name to read.
 * @return true when the name reports `kInvalidArgument`.
 */
bool IsInvalidArgument(std::wstring_view name)
{
    const auto parsed = appbox::ParseLogLevel(name);
    return !parsed.has_value() && parsed.error() == appbox::ErrorCode::kInvalidArgument;
}

} // namespace

/**
 * @brief Every level names its token, and every token reads back as its level.
 */
TEST(Unit_LogLevelName, EveryLevelRoundTripsThroughItsToken)
{
    for (const auto& expected : kTokens)
    {
        const wchar_t* token = appbox::LogLevelToken(expected.level);
        ASSERT_NE(token, nullptr);
        EXPECT_EQ(std::wstring(token), std::wstring(expected.token));

        const auto parsed = appbox::ParseLogLevel(token);
        ASSERT_TRUE(parsed.has_value()) << "the token " << Narrow(expected.token) << " is no level";
        EXPECT_EQ(parsed.value(), expected.level);
    }
}

/**
 * @brief The vocabulary names the seven levels of a run, and every token is
 *        distinct so that a name reads back as one level.
 */
TEST(Unit_LogLevelName, TheVocabularyIsComplete)
{
    EXPECT_EQ(kTokenCount, 7u);

    for (std::size_t left = 0; left < kTokenCount; ++left)
    {
        for (std::size_t right = left + 1; right < kTokenCount; ++right)
        {
            EXPECT_NE(std::wstring(kTokens[left].token), std::wstring(kTokens[right].token));
        }
    }
}

/**
 * @brief A name is read ignoring the ASCII case, so a run which spells `INFO`
 *        asks for the same level as a run which spells `info`.
 */
TEST(Unit_LogLevelName, TheCaseOfTheNameIsIgnored)
{
    for (const auto& expected : kTokens)
    {
        const std::wstring upper = UpperCase(expected.token);
        const auto         parsed_upper = appbox::ParseLogLevel(upper);
        ASSERT_TRUE(parsed_upper.has_value()) << "the name " << Narrow(upper) << " is no level";
        EXPECT_EQ(parsed_upper.value(), expected.level);

        const std::wstring swapped = SwappedCase(expected.token);
        const auto         parsed_swapped = appbox::ParseLogLevel(swapped);
        ASSERT_TRUE(parsed_swapped.has_value()) << "the name " << Narrow(swapped) << " is no level";
        EXPECT_EQ(parsed_swapped.value(), expected.level);
    }
}

/**
 * @brief A name which is no level reports `kInvalidArgument`, and an empty name
 *        reports it as well, because the caller has to tell a run which names no
 *        level from a run which names one.
 */
TEST(Unit_LogLevelName, AnUnknownNameIsAnInvalidArgument)
{
    EXPECT_TRUE(IsInvalidArgument(L""));
    EXPECT_TRUE(IsInvalidArgument(L"verbose"));
    EXPECT_TRUE(IsInvalidArgument(L"error"));
    EXPECT_TRUE(IsInvalidArgument(L"in"));
    EXPECT_TRUE(IsInvalidArgument(L"information"));
    EXPECT_TRUE(IsInvalidArgument(L" info"));
    EXPECT_TRUE(IsInvalidArgument(L"info "));
    EXPECT_TRUE(IsInvalidArgument(L"inf o"));
}

/**
 * @brief Only `off` asks for no log file, every other level asks for one.
 */
TEST(Unit_LogLevelName, OnlyOffAsksForNoLogFile)
{
    EXPECT_FALSE(appbox::LogLevelEnablesLogFile(appbox::LogLevel::Off));

    for (const auto& expected : kTokens)
    {
        EXPECT_EQ(appbox::LogLevelEnablesLogFile(expected.level), expected.level != appbox::LogLevel::Off);
    }
}
