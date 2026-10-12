#ifndef APPBOX_COMMON_SET_LOG_LEVEL_HPP
#define APPBOX_COMMON_SET_LOG_LEVEL_HPP

#include <spdlog/spdlog.h>
#include <string>

namespace appbox
{
namespace detail
{

/**
 * @brief Mapping of log level names to spdlog levels.
 */
struct LogLevelRecord
{
    const wchar_t*            name;
    spdlog::level::level_enum level;
};

inline constexpr LogLevelRecord kLogLevelRecords[] = {
    { L"trace",    spdlog::level::trace    },
    { L"debug",    spdlog::level::debug    },
    { L"info",     spdlog::level::info     },
    { L"warn",     spdlog::level::warn     },
    { L"err",      spdlog::level::err      },
    { L"critical", spdlog::level::critical },
    { L"off",      spdlog::level::off      },
};

} // namespace detail

/**
 * @brief Set the log level for the application.
 * @param[in] level Level string.
 */
inline void SetLogLevel(const std::wstring& level)
{
    for (const auto& record : detail::kLogLevelRecords)
    {
        if (level == record.name)
        {
            spdlog::set_level(record.level);
            return;
        }
    }
    SPDLOG_WARN(L"Unknown log level: {}", level);
}

/**
 * @brief Whether a level name asks for a log file.
 *
 * A run writes its log files only when it asks for a level: the name `off`, an
 * empty name (no level was given at all) and a name the table above does not
 * carry all report false, so a run which names no level writes nothing and
 * creates no directory.
 *
 * @param[in] level Level string.
 * @return true when the level asks for a log file.
 */
inline bool IsLogLevelEnabled(const std::wstring& level)
{
    if (level.empty())
    {
        return false;
    }

    for (const auto& record : detail::kLogLevelRecords)
    {
        if (level == record.name)
        {
            return record.level != spdlog::level::off;
        }
    }

    return false;
}

} // namespace appbox

#endif // APPBOX_COMMON_SET_LOG_LEVEL_HPP
