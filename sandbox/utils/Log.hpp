#ifndef APPBOX_SANDBOX_UTILS_LOG_HPP
#define APPBOX_SANDBOX_UTILS_LOG_HPP

#include <spdlog/spdlog.h>

#define LOG_EXT(LEVEL, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (spdlog::get_level() >= LEVEL)                                                                              \
        {                                                                                                              \
            SPDLOG_LOGGER_CALL(spdlog::default_logger_raw(), LEVEL, __VA_ARGS__);                                      \
        }                                                                                                              \
    } while (0)

#define LOG_T(...) LOG_EXT(spdlog::level::trace, __VA_ARGS__)
#define LOG_D(...) LOG_EXT(spdlog::level::debug, __VA_ARGS__)
#define LOG_I(...) LOG_EXT(spdlog::level::info, __VA_ARGS__)
#define LOG_W(...) LOG_EXT(spdlog::level::warn, __VA_ARGS__)
#define LOG_E(...) LOG_EXT(spdlog::level::err, __VA_ARGS__)
#define LOG_C(...) LOG_EXT(spdlog::level::critical, __VA_ARGS__)

#endif // APPBOX_SANDBOX_UTILS_LOG_HPP
