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
 * @brief Serialize a json value for the log.
 *
 * The value can hold bytes which are not valid UTF-8, because the parsed
 * parameters belong to the application. Replacing them keeps the message
 * readable instead of throwing, which the log path must never do.
 *
 * @param[in] value Value to serialize.
 * @return The serialized value.
 */
inline std::string DumpJson(const nlohmann::json& value)
{
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

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

        /*
         * The logger runs inside hooked kernel calls. It must never throw,
         * otherwise the exception unwinds through the hooked call and breaks
         * the caller: the parameters come from the application and can be
         * inconsistent or refer to memory which is not readable.
         */
        try
        {
            nlohmann::json data;
            data["method"] = method_;
            if (with_param)
            {
                data["param"] = fp_(std::forward<Args>(args)...);
            }

            const std::string line = DumpJson(data);
            appbox::Log(appbox::LOG_LEVEL_TRACE, method_.c_str(), 0, line);
        }
        catch (...)
        {
            /*
             * Unreachable sentinel: every parameter parser and conversion
             * helper returns a placeholder instead of throwing, so the
             * message is never lost. Reaching this point means a parser
             * regression and must be fixed there.
             */
            abort();
        }
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
 * The sink receives every message the sandbox reports, for example the sink
 * which appends it to the log file of the process. An empty sink means that
 * the messages are dropped, which is the case outside isolation mode.
 *
 * @param[in] req Log request.
 * @return true when the message was delivered, otherwise false.
 */
using LogSink = std::function<bool(const MsgLog::Req& req)>;

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

/**
 * @brief Set the lowest level the sandbox reports.
 *
 * The level is the level of the run: the launcher names it with
 * `--X-AppBox-LogLevel` and the injected configuration carries it, so a run
 * which asks for `info` never writes the trace of every kernel call of the
 * application into its log file.
 *
 * @param[in] level Lowest level which is reported.
 */
void SetLogLevel(MsgLogLevel level);

/**
 * @brief Set the lowest level the sandbox reports from its name.
 * @param[in] name Name of the level: `trace`, `debug`, `info`, `warn`, `err`,
 *                 `critical` or `off`.
 * @return true when the name is a level, false when it is not.
 */
bool SetLogLevelFromName(const std::string& name);

/**
 * @brief Open the log file of this process and report into it.
 *
 * The file is named after the process and is written by the process itself, so
 * its content survives a crash and the processes of one run never share a
 * file. A log file which cannot be created leaves the sandbox without a sink:
 * the messages are dropped, the process keeps running.
 *
 * @param[in] dir Directory of the log files of the run.
 * @param[in] image_path Path of the executable of this process.
 * @return true when the log file is open.
 */
bool OpenLogFile(const std::wstring& dir, const std::wstring& image_path);

/**
 * @brief Close the log file of this process, the messages are dropped afterwards.
 */
void CloseLogFile();

std::string    PointerToString(const void* ptr);
nlohmann::json ToJson(const POBJECT_ATTRIBUTES ObjectAttributes);
nlohmann::json ToJson(const PFILE_NETWORK_OPEN_INFORMATION FileInformation);
nlohmann::json ToJson(const PUNICODE_STRING FileName);
nlohmann::json DesiredAccessToJson(ACCESS_MASK DesiredAccess);

/**
 * @brief Convert a counted unicode string to UTF-8.
 *
 * The buffer of an UNICODE_STRING is not required to be null terminated and
 * the structure comes from the application, which may pass an inconsistent
 * length. Only the bytes covered by Length are read, never more, and a length
 * which exceeds MaximumLength is rejected.
 *
 * @param[in] str Unicode string. May be null.
 * @return The UTF-8 representation, empty when the string is not readable.
 */
std::string UnicodeStringToUTF8(const PUNICODE_STRING str);

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_LOG_HPP
