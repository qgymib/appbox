#ifndef APPBOX_COMMON_MSG_LOG_HPP
#define APPBOX_COMMON_MSG_LOG_HPP

#include <nlohmann/json.hpp>

namespace appbox
{

enum MsgLogLevel
{
    LOG_LEVEL_TRACE,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

struct MsgLog
{
    static constexpr const char* Method = "Log";

    struct Req
    {
        MsgLogLevel level;
        int64_t     time;
        std::string file;
        int         line;
        std::string payload;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, level, time, file, line, payload);
    };

    struct Rsp
    {
        int _ = 0; /* Ignore */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, _)
    };
};

} // namespace appbox

#endif // APPBOX_COMMON_MSG_LOG_HPP
