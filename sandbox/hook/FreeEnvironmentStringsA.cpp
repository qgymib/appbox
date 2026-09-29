#include "utils/WinAPI.h" /* Must be first include file */
#include "environment/Isolation.hpp"
#include "FreeEnvironmentStringsA.hpp"

T_FreeEnvironmentStringsA sys_FreeEnvironmentStringsA = nullptr;

/**
 * @brief Detour of FreeEnvironmentStringsA().
 *
 * An ANSI block which the sandbox handed out is released by the sandbox, which
 * owns it. Every other block is released by the operating system, exactly like
 * a call outside the sandbox would be.
 */
static BOOL WINAPI Hook_FreeEnvironmentStringsA(LPCH lpszEnvironmentBlock)
{
    if (appbox::environment::Isolation::ReleaseAnsiBlock(lpszEnvironmentBlock))
    {
        return TRUE;
    }

    return sys_FreeEnvironmentStringsA(lpszEnvironmentBlock);
}

static void LoadFreeEnvironmentStringsA()
{
    sys_FreeEnvironmentStringsA = reinterpret_cast<T_FreeEnvironmentStringsA>(
        appbox::environment::ResolveEnvironmentProc("FreeEnvironmentStringsA"));
}

appbox::HookRecord appbox::HookFreeEnvironmentStringsA = {
    "FreeEnvironmentStringsA",
    LoadFreeEnvironmentStringsA,
    (void**)&sys_FreeEnvironmentStringsA,
    Hook_FreeEnvironmentStringsA,
};
