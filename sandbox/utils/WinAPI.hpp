#ifndef APPBOX_COMMON_WINAPI_HPP
#define APPBOX_COMMON_WINAPI_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING;
typedef UNICODE_STRING* PUNICODE_STRING;

#ifdef __cplusplus
}
#endif

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
