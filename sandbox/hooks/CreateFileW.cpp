#include <Windows.h>
#include "__init__.hpp"

static HANDLE s_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                            LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                            DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    (void)lpFileName;
    (void)dwDesiredAccess;
    (void)dwShareMode;
    (void)lpSecurityAttributes;
    (void)dwCreationDisposition;
    (void)dwFlagsAndAttributes;
    (void)hTemplateFile;
    SetLastError(ERROR_NOT_SUPPORTED);
    return INVALID_HANDLE_VALUE;
}

appbox::hook::Func appbox::hook::CreateFileW = {
    L"Kernel32",
    L"CreateFileW",
    ::CreateFileW,
    s_CreateFileW,
};
