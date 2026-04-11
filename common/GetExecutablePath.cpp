#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <filesystem>
#include "GetExecutablePath.hpp"

std::wstring appbox::GetExecutableDir()
{
    std::wstring buf(MAX_PATH, L'\0');
    DWORD        len;
    while (true)
    {
        len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len < buf.size())
            break;
        buf.resize(buf.size() * 2);
    }
    buf.resize(len);
    return std::filesystem::path(buf).parent_path();
}
