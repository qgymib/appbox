#include <spdlog/spdlog.h>
#include "SetLogLevel.hpp"

struct LogLevelRecord
{
    const wchar_t*            name;
    spdlog::level::level_enum level;
};

static const LogLevelRecord log_level_records[] = {
    /* Log level mapping. */
    { L"trace",    spdlog::level::trace    },
    { L"debug",    spdlog::level::debug    },
    { L"info",     spdlog::level::info     },
    { L"warn",     spdlog::level::warn     },
    { L"err",      spdlog::level::err      },
    { L"critical", spdlog::level::critical },
    { L"off",      spdlog::level::off      },
};

void appbox::SetLogLevel(const std::wstring& level)
{
    for (const auto& record : log_level_records)
    {
        if (record.name == level)
        {
            spdlog::set_level(record.level);
            return;
        }
    }
    SPDLOG_WARN(L"Unknown log level: {}", level);
}
