#include <chrono>
#include <sstream>
#include <iostream>
#include <atomic>
#include <mutex>
#include "msg/Log.hpp"
#include "utils/BitParser.hpp"
#include "WString.hpp"
#include "Log.hpp"

/* Log output switch, can be toggled from any thread. */
static std::atomic<bool> s_log_enable = true;

/* Number of log messages which could not be delivered. */
static std::atomic<uint64_t> s_dropped_logs = 0;

/* Installed log sink and the lock which protects it. */
static std::mutex      s_log_sink_mutex;
static appbox::LogSink s_log_sink;

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

void appbox::Log(MsgLogLevel level, const char* file, int line, const std::wstring& msg)
{
    auto msgu8 = appbox::WideToUTF8(msg.c_str());
    appbox::Log(level, file, line, msgu8);
}

void appbox::Log(MsgLogLevel level, const char* file, int line, const std::string& msg)
{
    if (!s_log_enable.load(std::memory_order_relaxed))
    {
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

    nlohmann::json rsp;
    if (!sink(req, rsp))
    {
        /*
         * Never throw from the logging path: it is called from inside hooks and
         * an exception would unwind through the hooked kernel call.
         */
        s_dropped_logs.fetch_add(1, std::memory_order_relaxed);
    }
}

std::string appbox::PointerToString(const void* ptr)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(ptr);
    return oss.str();
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

nlohmann::json appbox::ToJson(const PUNICODE_STRING FileName)
{
    if (FileName == nullptr)
    {
        return nullptr;
    }

    nlohmann::json json;
    json["Length"] = FileName->Length;
    json["MaximumLength"] = FileName->MaximumLength;
    json["Buffer"] = appbox::WideToUTF8(FileName->Buffer);
    return json;
}

nlohmann::json appbox::DesiredAccessToJson(ACCESS_MASK DesiredAccess)
{
    return appbox::ParseBit(DesiredAccess, DesiredAccessMap, std::size(DesiredAccessMap));
}
