#include "utils/WinAPI.h" /* Must be first include file */
#include "environment/Isolation.hpp"
#include "FreeEnvironmentStringsW.hpp"

T_FreeEnvironmentStringsW sys_FreeEnvironmentStringsW = nullptr;

/**
 * @brief Detour of FreeEnvironmentStringsW().
 *
 * A block which the sandbox handed out is released by the sandbox, which owns
 * it. Every other block — the block of this process, which the sandbox never
 * reports, and a block of another source — is released by the operating
 * system, exactly like a call outside the sandbox would be.
 */
static BOOL WINAPI Hook_FreeEnvironmentStringsW(LPWCH lpszEnvironmentBlock)
{
    if (appbox::environment::Isolation::ReleaseBlock(lpszEnvironmentBlock))
    {
        return TRUE;
    }

    return sys_FreeEnvironmentStringsW(lpszEnvironmentBlock);
}

static void LoadFreeEnvironmentStringsW()
{
    sys_FreeEnvironmentStringsW = reinterpret_cast<T_FreeEnvironmentStringsW>(
        appbox::environment::ResolveEnvironmentProc("FreeEnvironmentStringsW"));
}

appbox::HookRecord appbox::HookFreeEnvironmentStringsW = {
    "FreeEnvironmentStringsW",
    LoadFreeEnvironmentStringsW,
    (void**)&sys_FreeEnvironmentStringsW,
    Hook_FreeEnvironmentStringsW,
};
