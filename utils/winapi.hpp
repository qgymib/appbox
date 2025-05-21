#ifndef APPBOX_UTILS_WINAPI_HPP
#define APPBOX_UTILS_WINAPI_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>
#include <string>
#include <stdexcept>
#include "macros.hpp"

namespace appbox
{

namespace winapi
{

typedef HANDLE (*CreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                              DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                              HANDLE hTemplateFile);

typedef HMODULE (*LoadLibraryW)(LPCWSTR lpLibFileName);

} // namespace winapi

/**
 * Retrieves the full path of the executable file of the current process.
 *
 * This function uses the Windows API to obtain the absolute path
 * to the executable file of the running application.
 *
 * @return A wide string containing the full path of the executable file.
 */
inline std::wstring GetExePath()
{
    wchar_t path[4096];
    GetModuleFileNameW(nullptr, path, ARRAY_SIZE(path));
    return path;
}

/**
 * Retrieves the size of a specified file in bytes.
 *
 * This function calculates and returns the size of a file
 * located with given file handle.
 *
 * @param[in] hFile The file handle whose size is to be determined.
 * @return The size of the file in bytes.
 */
inline uint64_t GetFileSize(HANDLE hFile)
{
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        throw std::runtime_error("GetFileSizeEx failed");
    }
    return fileSize.QuadPart;
}

} // namespace appbox

#endif
