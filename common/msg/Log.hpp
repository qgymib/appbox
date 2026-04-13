#ifndef APPBOX_COMMON_MSG_LOG_HPP
#define APPBOX_COMMON_MSG_LOG_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <nlohmann/json.hpp>

namespace appbox
{

struct MsgLog
{
    static constexpr const char* method = "Log";

    struct Req
    {
        std::string logger_name;
        int         level;
        uint64_t    time;
        size_t      thread_id;
        std::string filename;
        int         line;
        std::string funcname;
        std::string payload;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, logger_name, level, time, thread_id, filename, line,
                                       funcname, payload);
    };

    struct Rsp
    {
        int _ = 0; /* Ignore */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Rsp, _)
    };
};

} // namespace appbox

#endif // APPBOX_COMMON_MSG_LOG_HPP
