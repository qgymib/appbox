#include <Windows.h>
#include "wstring.hpp"
#include "winapi.hpp"
#include "__init__.hpp"

static HANDLE s_CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                            LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                            DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    std::wstring name = appbox::mbstowcs(lpFileName);
    auto f = reinterpret_cast<appbox::winapi::CreateFileW>(appbox::hook::CreateFileW.HookAddr);
    return f(name.c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes,
             dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

appbox::hook::Func appbox::hook::CreateFileA = {
    L"Kernel32",
    L"CreateFileA",
    ::CreateFileA,
    s_CreateFileA,
};
