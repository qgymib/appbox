#ifndef APPBOX_COMMON_MSG_LOG_HPP
#define APPBOX_COMMON_MSG_LOG_HPP

#include <nlohmann/json.hpp>

namespace appbox
{

enum MsgLogLevel
{
    LOGLEVEL_DEBUG = 0,
    LOGLEVEL_INFO = 1,
    LOGLEVEL_WARN = 2,
    LOGLEVEL_ERROR = 3,
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
