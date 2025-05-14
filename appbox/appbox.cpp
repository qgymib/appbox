#include <Windows.h>
#include "hooks/__init__.hpp"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL;
    (void)fdwReason;
    (void)lpvReserved;

    appbox::hook::Init();

    return TRUE;
}
