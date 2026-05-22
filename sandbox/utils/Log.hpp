#ifndef APPBOX_SANDBOX_UTILS_LOG_HPP
#define APPBOX_SANDBOX_UTILS_LOG_HPP

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "msg/Log.hpp"

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

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_LOG_HPP
