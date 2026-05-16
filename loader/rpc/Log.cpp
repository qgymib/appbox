#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include "sandbox/msg/Log.hpp"
#include "Loader.hpp"
#include "__init__.hpp"

static std::string epochMillisToLocalTimeString(int64_t epochMillis)
{
    // 1. 构造 time_point
    std::chrono::milliseconds                          ms(epochMillis);
    std::chrono::time_point<std::chrono::system_clock> tp(ms);

    // 2. 转为 time_t（秒级）
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);

    // 3. 转为本地时间
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &tt); // Windows 线程安全版本
#else
    localtime_r(&tt, &local_tm); // Linux 线程安全版本
#endif

    // 4. 格式化输出
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");

    // 5. 添加毫秒部分
    auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(tp);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(tp - seconds).count();

    oss << '.' << std::setw(3) << std::setfill('0') << milliseconds;

    return oss.str();
}

APPBOX_LOADER_RPC_DEFINE(MsgLog, id, param)
{
    /* Send response */
    wxGetApp().runtime->pipe_server->SendResponse(id, {});

    auto ts = epochMillisToLocalTimeString(param.time);
    auto msg = fmt::format("{} [{}:{}] {}", ts, param.file, param.line, param.payload);

    switch (param.level)
    {
    case appbox::LOGLEVEL_DEBUG:
        spdlog::debug("{}", msg);
        break;
    case appbox::LOGLEVEL_WARN:
        spdlog::warn("{}", msg);
        break;
    case appbox::LOGLEVEL_ERROR:
        spdlog::error("{}", msg);
        break;
    default:
        spdlog::info("{}", msg);
        break;
    }
}
