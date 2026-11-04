#ifndef APPBOX_COMMON_LOG_LEVEL_NAME_HPP
#define APPBOX_COMMON_LOG_LEVEL_NAME_HPP

/*
 * The vocabulary of the log levels of a run.
 *
 * The header names the levels and reads a level out of the text a command line
 * carries; it does not know a logging library, because the module which logs
 * owns the mapping of a level onto that library.
 */

#include <cstddef>
#include <string_view>
#include <tl/expected.hpp>
#include "ErrorCode.hpp"

namespace appbox
{

/**
 * @brief Level of the messages a run reports.
 *
 * The levels are those of the option which names the level of a run, in the
 * order of growing severity. `Off` reports nothing at all.
 */
enum class LogLevel
{
    Trace,    /* Every message, including the trace of every kernel call. */
    Debug,    /* Diagnostic messages of the development of a module. */
    Info,     /* Milestones of a run. */
    Warn,     /* A condition the run survives. */
    Error,    /* A request the run cannot complete. */
    Critical, /* A failure which ends the run. */
    Off,      /* No message at all. */
};

namespace detail
{

/**
 * @brief Text of one level, as a command line spells it.
 *
 * The text of `LogLevel::Error` is `"err"`, which is the spelling the option of
 * the launcher and the log file of the sandbox accept.
 */
inline constexpr const wchar_t* kLogLevelTokens[] = {
    L"trace", L"debug", L"info", L"warn", L"err", L"critical", L"off",
};

/** Number of levels the table above names. */
inline constexpr std::size_t kLogLevelCount = sizeof(kLogLevelTokens) / sizeof(kLogLevelTokens[0]);

/**
 * @brief Fold one character of a level name to lower case.
 *
 * Only the ASCII letters are folded, so the comparison never depends on the
 * locale of the process, which a test and a service do not share.
 *
 * @param[in] character Character to fold.
 * @return The lower case character, the character itself when it is no ASCII
 *         upper case letter.
 */
constexpr wchar_t FoldLogLevelCharacter(wchar_t character) noexcept
{
    return (character >= L'A' && character <= L'Z') ? static_cast<wchar_t>(character - L'A' + L'a') : character;
}

/**
 * @brief Whether a text is the token of a level, ignoring the case.
 * @param[in] name Text to compare.
 * @param[in] token Token to compare with.
 * @return true when the two texts differ in case alone.
 */
constexpr bool LogLevelTokenMatches(std::wstring_view name, std::wstring_view token) noexcept
{
    if (name.size() != token.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < name.size(); ++index)
    {
        if (FoldLogLevelCharacter(name[index]) != FoldLogLevelCharacter(token[index]))
        {
            return false;
        }
    }

    return true;
}

} // namespace detail

/**
 * @brief Read the level a name spells.
 *
 * The name is compared ignoring the ASCII case, and the whole name has to spell
 * a level: an empty name and a name which is no level report `kInvalidArgument`,
 * because the caller has to tell a run which names no level at all from a run
 * which names a level.
 *
 * @param[in] name Name of the level.
 * @return The level, or `kInvalidArgument` when the name is no level.
 */
inline tl::expected<LogLevel, ErrorCode> ParseLogLevel(std::wstring_view name) noexcept
{
    for (std::size_t index = 0; index < detail::kLogLevelCount; ++index)
    {
        if (detail::LogLevelTokenMatches(name, detail::kLogLevelTokens[index]))
        {
            return static_cast<LogLevel>(index);
        }
    }

    return tl::unexpected<ErrorCode>(ErrorCode::kInvalidArgument);
}

/**
 * @brief The text a command line spells a level with.
 *
 * @param[in] level Level to name.
 * @return The token of the level, empty for a value which is no level.
 */
constexpr const wchar_t* LogLevelToken(LogLevel level) noexcept
{
    const auto index = static_cast<std::size_t>(level);
    if (index >= detail::kLogLevelCount)
    {
        return L"";
    }

    return detail::kLogLevelTokens[index];
}

/**
 * @brief Whether a level asks for a log file.
 *
 * Every level but `Off` asks for one: a run which names `Off` creates neither a
 * directory nor a file.
 *
 * @param[in] level Level of the run.
 * @return true when the level asks for a log file.
 */
constexpr bool LogLevelEnablesLogFile(LogLevel level) noexcept
{
    return level != LogLevel::Off;
}

} // namespace appbox

#endif // APPBOX_COMMON_LOG_LEVEL_NAME_HPP
