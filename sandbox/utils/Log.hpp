#ifndef APPBOX_SANDBOX_UTILS_LOG_HPP
#define APPBOX_SANDBOX_UTILS_LOG_HPP

#include <spdlog/spdlog.h>
#include "msg/Log.hpp"

#define LOG_GENERIC(LEVEL, FMT, ...)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        auto msg = fmt::format(FMT, ##__VA_ARGS__);                                                                    \
        appbox::Log(LEVEL, __FILE__, __LINE__, msg);                                                                   \
    } while (0)

#define LOG_D(FMT, ...) LOG_GENERIC(appbox::LOGLEVEL_DEBUG, FMT, ##__VA_ARGS__)
#define LOG_I(FMT, ...) LOG_GENERIC(appbox::LOGLEVEL_INFO, FMT, ##__VA_ARGS__)
#define LOG_W(FMT, ...) LOG_GENERIC(appbox::LOGLEVEL_WARN, FMT, ##__VA_ARGS__)
#define LOG_E(FMT, ...) LOG_GENERIC(appbox::LOGLEVEL_ERROR, FMT, ##__VA_ARGS__)

#define THROW_LOG(FMT, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        auto msg = fmt::format(FMT, ##__VA_ARGS__);                                                                    \
        appbox::Log(appbox::LOGLEVEL_ERROR, __FILE__, __LINE__, msg);                                                  \
        throw std::runtime_error(msg);                                                                                 \
    } while (0)

namespace appbox
{

void Log(MsgLogLevel level, const char* file, int line, const std::string& msg);
void Log(MsgLogLevel level, const char* file, int line, const std::wstring& msg);

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_LOG_HPP
