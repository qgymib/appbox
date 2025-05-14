#include <Windows.h>
#include "__init__.hpp"

static HMODULE s_LoadLibraryW(LPCWSTR lpLibFileName)
{
    (void)lpLibFileName;
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}

appbox::hook::Func appbox::hook::LoadLibraryW = {
    L"Kernel32",
    L"LoadLibraryW",
    ::LoadLibraryW,
    s_LoadLibraryW,
};
