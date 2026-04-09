#include <spdlog/spdlog.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <chrono>
#include <mutex>
#include "msg/Log.hpp"
#include "Sandbox.hpp"
#include "RemoteLog.hpp"

template <typename Mutex>
class RemoteLogSink : public spdlog::sinks::base_sink<Mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        appbox::MsgLog::Req req;
        req.logger_name = msg.logger_name.data();
        req.level = msg.level;
        req.time =
            std::chrono::duration_cast<std::chrono::milliseconds>(msg.time.time_since_epoch())
                .count();
        req.thread_id = msg.thread_id;
        req.filename = msg.source.filename;
        req.line = msg.source.line;
        req.funcname = msg.source.funcname;
        req.payload = msg.payload.data();

        (*appbox::sandbox->client)->Call<appbox::MsgLog>(req);
    }

    void flush_() override
    {
    }
};

void appbox::RemoteLogInit()
{
    auto log_sink = std::make_shared<RemoteLogSink<std::mutex>>();
    spdlog::get("")->sinks().push_back(log_sink);
}
