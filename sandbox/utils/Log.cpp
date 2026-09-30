#include <chrono>
#include <ctime>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include "msg/Log.hpp"
#include "utils/BitParser.hpp"
#include "utils/LogFile.hpp"
#include "WString.hpp"
#include "Log.hpp"

/* Log output switch, can be toggled from any thread. */
static std::atomic<bool> s_log_enable = true;

/* Number of log messages which could not be delivered. */
static std::atomic<uint64_t> s_dropped_logs = 0;

/* Lowest level which is reported; every message is reported until a run says otherwise. */
static std::atomic<appbox::MsgLogLevel> s_log_level{ appbox::LOG_LEVEL_TRACE };

/* Installed log sink and the lock which protects it. */
static std::mutex      s_log_sink_mutex;
static appbox::LogSink s_log_sink;

namespace
{

/** Names of the levels, in the order of `MsgLogLevel`. */
constexpr const char* kLevelNames[] = { "trace", "debug", "info", "warn", "error", "off" };

/**
 * @brief Name of a level.
 * @param[in] level Level to name.
 * @return The name, `unknown` for a value the enumeration does not hold.
 */
const char* LevelName(appbox::MsgLogLevel level)
{
    const auto index = static_cast<std::size_t>(level);
    if (index >= std::size(kLevelNames))
    {
        return "unknown";
    }

    return kLevelNames[index];
}

/** Digits of a hexadecimal number, in the order of their value. */
constexpr const char kHexDigits[] = "0123456789abcdef";

/**
 * @brief Append a number in decimal form.
 *
 * The number is written by hand: the log path runs inside hooks, and a stream
 * of the C++ library builds its text through the locale of the process, which
 * is not usable there (see `FormatMessage()`).
 *
 * @param[in,out] text Text which receives the number.
 * @param[in] value Number to append.
 * @param[in] width Number of the digits, padded with zeros.
 */
void AppendDecimal(std::string& text, unsigned long long value, std::size_t width)
{
    char        buffer[20];
    std::size_t count = 0;

    do
    {
        buffer[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);

    while (count < width)
    {
        buffer[count++] = '0';
    }

    while (count > 0)
    {
        text.push_back(buffer[--count]);
    }
}

/**
 * @brief Append a number in hexadecimal form.
 * @param[in,out] text Text which receives the number.
 * @param[in] value Number to append.
 */
void AppendHex(std::string& text, unsigned long long value)
{
    char        buffer[16];
    std::size_t count = 0;

    do
    {
        buffer[count++] = kHexDigits[value & 0xF];
        value >>= 4;
    } while (value != 0);

    while (count > 0)
    {
        text.push_back(buffer[--count]);
    }
}

/**
 * @brief Format the time of a message.
 *
 * The time is formatted by hand instead of through a stream of the C++ library:
 * the log path runs inside hooks and inside the loader lock, where the locale
 * machinery of the C++ library is not usable, and a message must never be lost
 * because of the way it is written.
 *
 * @param[in] epoch_millis Time of the message, in milliseconds since the epoch.
 * @return The local time with milliseconds.
 */
std::string FormatTime(int64_t epoch_millis)
{
    const std::chrono::milliseconds                          ms(epoch_millis);
    const std::chrono::time_point<std::chrono::system_clock> tp(ms);
    const std::time_t                                        tt = std::chrono::system_clock::to_time_t(tp);

    std::tm local_tm{};
    localtime_s(&local_tm, &tt);

    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(tp);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(tp - seconds).count();

    std::string text;
    text.reserve(23);

    AppendDecimal(text, static_cast<unsigned long long>(local_tm.tm_year + 1900), 4);
    text.push_back('-');
    AppendDecimal(text, static_cast<unsigned long long>(local_tm.tm_mon + 1), 2);
    text.push_back('-');
    AppendDecimal(text, static_cast<unsigned long long>(local_tm.tm_mday), 2);
    text.push_back(' ');
    AppendDecimal(text, static_cast<unsigned long long>(local_tm.tm_hour), 2);
    text.push_back(':');
    AppendDecimal(text, static_cast<unsigned long long>(local_tm.tm_min), 2);
    text.push_back(':');
    AppendDecimal(text, static_cast<unsigned long long>(local_tm.tm_sec), 2);
    text.push_back('.');
    AppendDecimal(text, static_cast<unsigned long long>(millis), 3);

    return text;
}

/**
 * @brief Format one message as one line of the log file of the process.
 * @param[in] req Message to format.
 * @return The line without its line break.
 */
std::string FormatMessage(const appbox::MsgLog::Req& req)
{
    return fmt::format("{} [{}] [{}:{}] {}", FormatTime(req.time), LevelName(req.level), req.file, req.line,
                       req.payload);
}

} // namespace

static const appbox::BitData DesiredAccessMap[] = {
    /* Combination flags always go first */
    { "FILE_GENERIC_WRITE",    FILE_GENERIC_WRITE    },
    { "FILE_GENERIC_READ",     FILE_GENERIC_READ     },
    { "FILE_GENERIC_EXECUTE",  FILE_GENERIC_EXECUTE  },
    /* Individual flags */
    { "DELETE",                DELETE                },
    { "FILE_READ_DATA",        FILE_READ_DATA        },
    { "FILE_READ_ATTRIBUTES",  FILE_READ_ATTRIBUTES  },
    { "FILE_READ_EA",          FILE_READ_EA          },
    { "READ_CONTROL",          READ_CONTROL          },
    { "FILE_WRITE_DATA",       FILE_WRITE_DATA       },
    { "FILE_WRITE_ATTRIBUTES", FILE_WRITE_ATTRIBUTES },
    { "FILE_WRITE_EA",         FILE_WRITE_EA         },
    { "FILE_APPEND_DATA",      FILE_APPEND_DATA      },
    { "WRITE_DAC",             WRITE_DAC             },
    { "WRITE_OWNER",           WRITE_OWNER           },
    { "SYNCHRONIZE",           SYNCHRONIZE           },
    { "FILE_EXECUTE",          FILE_EXECUTE          },
    { "GENERIC_READ",          GENERIC_READ          },
    { "GENERIC_WRITE",         GENERIC_WRITE         },
};

static const char* get_filename(const char* file)
{
    const char* pos = file;

    for (; *file; ++file)
    {
        if (*file == '\\' || *file == '/')
        {
            pos = file + 1;
        }
    }
    return pos;
}

appbox::LogGuard::LogGuard()
{
    LogEnable(false);
}

appbox::LogGuard::~LogGuard()
{
    LogEnable(true);
}

void appbox::LogEnable(bool enable)
{
    s_log_enable.store(enable, std::memory_order_relaxed);
}

void appbox::SetLogSink(LogSink sink)
{
    std::lock_guard<std::mutex> lock(s_log_sink_mutex);
    s_log_sink = std::move(sink);
}

uint64_t appbox::DroppedLogCount()
{
    return s_dropped_logs.load(std::memory_order_relaxed);
}

void appbox::SetLogLevel(MsgLogLevel level)
{
    s_log_level.store(level, std::memory_order_relaxed);
}

bool appbox::SetLogLevelFromName(const std::string& name)
{
    /*
     * The names are the names of the option of the launcher, so a run names its
     * level once and the sandbox reports exactly the levels the run asked for.
     */
    static const struct
    {
        const char* name;
        MsgLogLevel level;
    } kLevels[] = {
        { "trace",    LOG_LEVEL_TRACE },
        { "debug",    LOG_LEVEL_DEBUG },
        { "info",     LOG_LEVEL_INFO  },
        { "warn",     LOG_LEVEL_WARN  },
        { "err",      LOG_LEVEL_ERROR },
        { "critical", LOG_LEVEL_ERROR },
        { "off",      LOG_LEVEL_OFF   },
    };

    for (const auto& record : kLevels)
    {
        if (name == record.name)
        {
            SetLogLevel(record.level);
            return true;
        }
    }

    return false;
}

bool appbox::OpenLogFile(const std::wstring& dir, const std::wstring& image_path)
{
    if (!LogFile::Open(dir, image_path))
    {
        return false;
    }

    SetLogSink([](const MsgLog::Req& req) -> bool {
        LogFile::WriteLine(FormatMessage(req));
        return LogFile::IsOpen();
    });

    return true;
}

void appbox::CloseLogFile()
{
    SetLogSink(nullptr);
    LogFile::Close();
}

void appbox::Log(MsgLogLevel level, const char* file, int line, const std::wstring& msg)
{
    /*
     * The logging path runs inside hooks, so a conversion which fails must
     * not throw. The message is reported as dropped instead, exactly like a
     * message which the sink could not deliver.
     */
    std::string msgu8;
    try
    {
        msgu8 = appbox::WideToUTF8(msg.c_str());
    }
    catch (...)
    {
        s_dropped_logs.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    appbox::Log(level, file, line, msgu8);
}

void appbox::Log(MsgLogLevel level, const char* file, int line, const std::string& msg)
{
    if (!s_log_enable.load(std::memory_order_relaxed))
    {
        return;
    }

    if (level < s_log_level.load(std::memory_order_relaxed))
    {
        /* The run asked for a level which is above this message. */
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

    appbox::MsgLog::Req req;
    req.level = level;
    req.time = static_cast<uint64_t>(duration_ms.count());
    req.file = get_filename(file);
    req.line = line;
    req.payload = msg;

    LogSink sink;
    {
        std::lock_guard<std::mutex> lock(s_log_sink_mutex);
        sink = s_log_sink;
    }

    if (!sink)
    {
        /* No sink installed, for example outside isolation mode. */
        return;
    }

    /*
     * Never throw from the logging path: it is called from inside hooks and an
     * exception would unwind through the hooked kernel call. The sink writes
     * to a file, so even a successful looking call can fail on a full disk;
     * such a failure is reported as a dropped message.
     */
    bool delivered = false;
    try
    {
        delivered = sink(req);
    }
    catch (...)
    {
        delivered = false;
    }

    if (!delivered)
    {
        s_dropped_logs.fetch_add(1, std::memory_order_relaxed);
    }
}

std::string appbox::PointerToString(const void* ptr)
{
    /*
     * The pointer is formatted by hand, for the same reason as the time of a
     * message (see `FormatTime()`): the log path runs inside hooks, where a
     * stream of the C++ library must not be used.
     */
    std::string text = "0x";
    AppendHex(text, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ptr)));
    return text;
}

nlohmann::json appbox::ToJson(const POBJECT_ATTRIBUTES ObjectAttributes)
{
    if (ObjectAttributes == nullptr)
    {
        return nullptr;
    }

    nlohmann::json json;
    json["Length"] = ObjectAttributes->Length;
    json["RootDirectory"] = appbox::PointerToString(ObjectAttributes->RootDirectory);
    if (ObjectAttributes->ObjectName != nullptr)
    {
        json["ObjectName"] = appbox::ToJson(ObjectAttributes->ObjectName);
    }
    json["Attributes"] = ObjectAttributes->Attributes;
    json["SecurityDescriptor"] = appbox::PointerToString(ObjectAttributes->SecurityDescriptor);
    json["SecurityQualityOfService"] = appbox::PointerToString(ObjectAttributes->SecurityQualityOfService);
    return json;
}

nlohmann::json appbox::ToJson(const PFILE_NETWORK_OPEN_INFORMATION FileInformation)
{
    if (FileInformation == nullptr)
    {
        return nullptr;
    }

    nlohmann::json json;
    json["CreationTime"] = FileInformation->CreationTime.QuadPart;
    json["LastAccessTime"] = FileInformation->LastAccessTime.QuadPart;
    json["LastWriteTime"] = FileInformation->LastWriteTime.QuadPart;
    json["ChangeTime"] = FileInformation->ChangeTime.QuadPart;
    json["AllocationSize"] = FileInformation->AllocationSize.QuadPart;
    json["EndOfFile"] = FileInformation->EndOfFile.QuadPart;
    json["FileAttributes"] = FileInformation->FileAttributes;
    return json;
}

std::string appbox::UnicodeStringToUTF8(const PUNICODE_STRING str)
{
    if (str == nullptr || str->Buffer == nullptr || str->Length == 0)
    {
        return std::string();
    }

    /*
     * A caller can pass a length which does not fit into the buffer it owns,
     * for example a probe which looks for a hook that reads the buffer blindly.
     * Such a string is not read at all.
     */
    if (str->Length > str->MaximumLength || (str->Length % sizeof(wchar_t)) != 0)
    {
        return std::string();
    }

    /*
     * The buffer is counted, not terminated, so it is copied by length first:
     * reading it as a C string would leave the buffer of the caller.
     */
    const std::wstring text(str->Buffer, static_cast<size_t>(str->Length) / sizeof(wchar_t));

    /*
     * The conversion itself can fail, for example on an unpaired surrogate.
     * This function is called from hooks, so it reports an empty string
     * instead of letting the exception escape.
     */
    try
    {
        return appbox::WideToUTF8(text);
    }
    catch (...)
    {
        return std::string();
    }
}

nlohmann::json appbox::ToJson(const PUNICODE_STRING FileName)
{
    if (FileName == nullptr)
    {
        return nullptr;
    }

    nlohmann::json json;
    json["Length"] = FileName->Length;
    json["MaximumLength"] = FileName->MaximumLength;

    /*
     * The buffer can be null while the structure itself is not, for example
     * for the Class parameter of NtCreateKey(). The logging path must never
     * throw, because the exception would unwind through the hooked call.
     */
    if (FileName->Buffer == nullptr || FileName->Length == 0)
    {
        json["Buffer"] = "";
    }
    else
    {
        json["Buffer"] = appbox::UnicodeStringToUTF8(FileName);
    }
    return json;
}

nlohmann::json appbox::DesiredAccessToJson(ACCESS_MASK DesiredAccess)
{
    return appbox::ParseBit(DesiredAccess, DesiredAccessMap, std::size(DesiredAccessMap));
}
