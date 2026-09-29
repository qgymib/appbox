#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "environment/Isolation.hpp"
#include "GetEnvironmentStringsA.hpp"

T_GetEnvironmentStringsA sys_GetEnvironmentStringsA = nullptr;

/**
 * @brief Detour of GetEnvironmentStringsA().
 *
 * The block which is reported is the one of the sandbox, converted into the
 * ANSI code page of the caller, and never the block of this process: the block
 * of the process holds the environment of the host, so reporting it would show
 * every variable an isolation mode hides. The caller releases the block with
 * FreeEnvironmentStringsA(), which the sandbox answers for its own blocks.
 */
static LPCH WINAPI Hook_GetEnvironmentStringsA()
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_GetEnvironmentStringsA();
    }

    LPCH block = appbox::environment::Isolation::CreateAnsiBlock();
    if (block == nullptr)
    {
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }

    return block;
}

static void LoadGetEnvironmentStringsA()
{
    sys_GetEnvironmentStringsA = reinterpret_cast<T_GetEnvironmentStringsA>(
        appbox::environment::ResolveEnvironmentProc("GetEnvironmentStringsA"));
}

appbox::HookRecord appbox::HookGetEnvironmentStringsA = {
    "GetEnvironmentStringsA",
    LoadGetEnvironmentStringsA,
    (void**)&sys_GetEnvironmentStringsA,
    Hook_GetEnvironmentStringsA,
};
