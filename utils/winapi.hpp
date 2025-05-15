#ifndef APPBOX_UTILS_WINAPI_HPP
#define APPBOX_UTILS_WINAPI_HPP

#include <Windows.h>
#include "wstring.hpp"

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
std::wstring exepath();

uint64_t GetFileSize(HANDLE hFile);

} // namespace appbox

#endif
