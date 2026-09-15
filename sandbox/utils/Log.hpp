#ifndef APPBOX_SANDBOX_UTILS_LOG_HPP
#define APPBOX_SANDBOX_UTILS_LOG_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include "msg/Log.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <string>

#define LOG_GENERIC(LEVEL, FMT, ...)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        auto _msg = fmt::format(FMT, ##__VA_ARGS__);                                                                   \
        appbox::Log(LEVEL, __FILE__, __LINE__, _msg);                                                                  \
    } while (0)

#define LOG_T(FMT, ...) LOG_GENERIC(appbox::LOG_LEVEL_TRACE, FMT, ##__VA_ARGS__)
#define LOG_D(FMT, ...) LOG_GENERIC(appbox::LOG_LEVEL_DEBUG, FMT, ##__VA_ARGS__)
#define LOG_I(FMT, ...) LOG_GENERIC(appbox::LOG_LEVEL_INFO, FMT, ##__VA_ARGS__)
#define LOG_W(FMT, ...) LOG_GENERIC(appbox::LOG_LEVEL_WARN, FMT, ##__VA_ARGS__)
#define LOG_E(FMT, ...) LOG_GENERIC(appbox::LOG_LEVEL_ERROR, FMT, ##__VA_ARGS__)

#define THROW_LOG(FMT, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        auto _msg = fmt::format(FMT, ##__VA_ARGS__);                                                                   \
        appbox::Log(appbox::LOG_LEVEL_ERROR, __FILE__, __LINE__, _msg);                                                \
        throw std::runtime_error(_msg);                                                                                \
    } while (0)

namespace appbox
{

/**
 * @brief Function logger.
 */
template <typename Fp>
struct LoggerF
{
    LoggerF(const std::string& method, Fp fp) : method_(method), fp_(fp), is_activate(true), with_param(true)
    {
    }

    template <typename... Args>
    void Log(Args&&... args)
    {
        if (!is_activate)
        {
            return;
        }

        nlohmann::json data;
        data["method"] = method_;
        if (with_param)
        {
            data["param"] = fp_(std::forward<Args>(args)...);
        }

        appbox::Log(appbox::LOG_LEVEL_TRACE, method_.c_str(), 0, data.dump());
    }

    std::string method_;     /* Method name.*/
    Fp          fp_;         /* Parameter parser function. */
    bool        is_activate; /* Function logger activate flag. */
    bool        with_param;  /* Parameter output flag. */
};

/**
 * @brief Disable log temporary
 */
struct LogGuard
{
    LogGuard();
    ~LogGuard();
};

/**
 * @brief Log sink.
 *
 * The sandbox installs a sink which forwards the log message to the loader over
 * the RPC pipe. An empty sink means that log messages are dropped, which is the
 * case outside isolation mode.
 *
 * @param[in] req Log request.
 * @param[out] rsp Log response.
 * @return true when the message was delivered, otherwise false.
 */
using LogSink = std::function<bool(const MsgLog::Req& req, nlohmann::json& rsp)>;

/**
 * @brief Install or uninstall the log sink.
 * @param[in] sink Sink callback. Pass an empty function to uninstall the sink.
 */
void SetLogSink(LogSink sink);

/**
 * @brief Number of log messages which could not be delivered.
 * @return Number of dropped messages.
 */
uint64_t DroppedLogCount();

void Log(MsgLogLevel level, const char* file, int line, const std::string& msg);
void Log(MsgLogLevel level, const char* file, int line, const std::wstring& msg);

/**
 * @brief Enable or disable log output.
 * @param[in] enable Enable flag.
 */
void LogEnable(bool enable);

std::string    PointerToString(const void* ptr);
nlohmann::json ToJson(const POBJECT_ATTRIBUTES ObjectAttributes);
nlohmann::json ToJson(const PFILE_NETWORK_OPEN_INFORMATION FileInformation);
nlohmann::json ToJson(const PUNICODE_STRING FileName);
nlohmann::json DesiredAccessToJson(ACCESS_MASK DesiredAccess);

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_LOG_HPP
