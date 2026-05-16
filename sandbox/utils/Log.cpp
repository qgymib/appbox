#include <chrono>
#include "msg/Log.hpp"
#include "WString.hpp"
#include "Sandbox.hpp"
#include "Log.hpp"

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

void appbox::Log(MsgLogLevel level, const char* file, int line, const std::wstring& msg)
{
    auto msgu8 = appbox::WideToUTF8(msg.c_str());
    appbox::Log(level, file, line, msgu8);
}

void appbox::Log(MsgLogLevel level, const char* file, int line, const std::string& msg)
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

    appbox::MsgLog::Req req;
    req.level = level;
    req.time = static_cast<uint64_t>(duration_ms.count());
    req.file = get_filename(file);
    req.line = line;
    req.payload = msg;

    (*appbox::sandbox->client)->Call<appbox::MsgLog>(req);
}
