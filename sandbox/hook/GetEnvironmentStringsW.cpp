#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "environment/Isolation.hpp"
#include "GetEnvironmentStringsW.hpp"

T_GetEnvironmentStringsW sys_GetEnvironmentStringsW = nullptr;

/**
 * @brief Detour of GetEnvironmentStringsW().
 *
 * The block which is reported is the one of the sandbox and never the block of
 * this process: the block of the process holds the environment of the host, so
 * reporting it would show every variable an isolation mode hides. The caller
 * owns the block and releases it with FreeEnvironmentStringsW(), which the
 * sandbox answers for its own blocks.
 *
 * The runtime of the packaged application reads its environment with this call
 * while the process starts, so the block is the one the application sees.
 */
static LPWCH WINAPI Hook_GetEnvironmentStringsW()
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_GetEnvironmentStringsW();
    }

    LPWCH block = appbox::environment::Isolation::CreateBlock();
    if (block == nullptr)
    {
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }

    return block;
}

static void LoadGetEnvironmentStringsW()
{
    sys_GetEnvironmentStringsW = reinterpret_cast<T_GetEnvironmentStringsW>(
        appbox::environment::ResolveEnvironmentProc("GetEnvironmentStringsW"));
}

appbox::HookRecord appbox::HookGetEnvironmentStringsW = {
    "GetEnvironmentStringsW",
    LoadGetEnvironmentStringsW,
    (void**)&sys_GetEnvironmentStringsW,
    Hook_GetEnvironmentStringsW,
};
