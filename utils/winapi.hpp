#ifndef APPBOX_UTILS_WINAPI_HPP
#define APPBOX_UTILS_WINAPI_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>
#include <string>

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
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

inline uint64_t GetFileSize(HANDLE hFile)
{
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        throw std::exception("GetFileSizeEx failed");
    }
    return fileSize.QuadPart;
}

} // namespace appbox

#endif
