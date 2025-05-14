#ifndef APPBOX_UTILS_WINAPI_HPP
#define APPBOX_UTILS_WINAPI_HPP

#include <Windows.h>

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

} // namespace appbox

#endif
