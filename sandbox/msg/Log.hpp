#ifndef APPBOX_COMMON_MSG_LOG_HPP
#define APPBOX_COMMON_MSG_LOG_HPP

#include <cstdint>
#include <string>

namespace appbox
{

/**
 * @brief Level of a message the sandbox reports.
 *
 * The levels are ordered: a level which is not lower than the level of the run
 * is reported, so `LOG_LEVEL_OFF` reports nothing at all.
 */
enum MsgLogLevel
{
    LOG_LEVEL_TRACE,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_OFF,
};

/**
 * @brief One message the sandbox reports.
 *
 * The message is written to the log file of the process which produced it (see
 * `sandbox/utils/LogFile.hpp`), so the record carries the level, the time and
 * the place the message was raised at.
 */
struct MsgLog
{
    struct Req
    {
        MsgLogLevel level;   /* Level of the message. */
        int64_t     time;    /* Time the message was raised at, in milliseconds since the epoch. */
        std::string file;    /* Name of the source file the message was raised in. */
        int         line;    /* Line of the source file the message was raised at. */
        std::string payload; /* Text of the message. */
    };
};

} // namespace appbox

#endif // APPBOX_COMMON_MSG_LOG_HPP
