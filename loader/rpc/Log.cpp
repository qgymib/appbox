#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>
#include "msg/Log.hpp"
#include "Loader.hpp"
#include "__init__.hpp"

APPBOX_LOADER_RPC_DEFINE(MsgLog, id, param)
{
    /* Send response */
    appbox::loader->pipe_server->SendResponse(id, {});

    auto logger = spdlog::get(param.logger_name);
    if (!logger)
    {
        return;
    }

    auto level = static_cast<spdlog::level::level_enum>(param.level);
    if (!logger->should_log(level))
    {
        return;
    }

    auto time_point =
        spdlog::log_clock::time_point(std::chrono::duration_cast<spdlog::log_clock::duration>(
            std::chrono::milliseconds(param.time)));

    spdlog::source_loc       loc{ param.filename.c_str(), param.line, param.funcname.c_str() };
    spdlog::details::log_msg msg(time_point, loc, param.logger_name, level, param.payload);
    msg.thread_id = param.thread_id;

    for (auto& sink : logger->sinks())
    {
        if (sink->should_log(level))
        {
            try
            {
                sink->log(msg);
            }
            catch (const std::exception& ex)
            {
                // 处理异常，避免一个 sink 失败影响其他 sink
                SPDLOG_ERROR("Failed to log message with level {}: {}", param.level, ex.what());
            }
        }
    }
}
