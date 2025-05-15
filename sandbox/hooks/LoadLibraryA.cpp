#include <Windows.h>
#include "wstring.hpp"
#include "winapi.hpp"
#include "__init__.hpp"

static HMODULE s_LoadLibraryA(LPCSTR lpLibFileName)
{
    std::wstring name = appbox::mbstowcs(lpLibFileName);
    auto f = reinterpret_cast<appbox::winapi::LoadLibraryW>(appbox::hook::LoadLibraryW.HookAddr);
    return f(name.c_str());
}

appbox::hook::Func appbox::hook::LoadLibraryA = {
    L"Kernel32",
    L"LoadLibraryA",
    ::LoadLibraryA,
    s_LoadLibraryA,
};
