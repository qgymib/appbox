#include <chrono>
#include <sstream>
#include <iostream>
#include <mutex>
#include "msg/Log.hpp"
#include "WString.hpp"
#include "Sandbox.hpp"
#include "Log.hpp"

static volatile bool s_log_enable = true;

const char* get_filename(const char* file)
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

void appbox::LogEnable(bool enable)
{
    s_log_enable = enable;
}

void appbox::Log(MsgLogLevel level, const char* file, int line, const std::wstring& msg)
{
    auto msgu8 = appbox::WideToUTF8(msg.c_str());
    appbox::Log(level, file, line, msgu8);
}

void appbox::Log(MsgLogLevel level, const char* file, int line, const std::string& msg)
{
    if (!s_log_enable)
    {
        return;
    }

#if 1
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

    appbox::MsgLog::Req req;
    req.level = level;
    req.time = static_cast<uint64_t>(duration_ms.count());
    req.file = get_filename(file);
    req.line = line;
    req.payload = msg;

    nlohmann::json rsp;
    if (!appbox::sandbox->client->Call(appbox::MsgLog::Method, req, rsp))
    {
        throw std::runtime_error("Failed to call log");
    }
#else
    (void)level;
    auto data = fmt::format("[{}:{}] {}\n", file, line, msg);

    {
        static std::mutex           s_log_mutex;
        std::lock_guard<std::mutex> lock(s_log_mutex);
        std::ofstream               ofs("sandbox.log", std::ios::binary | std::ios::app);
        ofs.write(data.data(), data.size());
    }

#endif
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
