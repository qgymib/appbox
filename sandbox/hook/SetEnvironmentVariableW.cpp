#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "environment/Isolation.hpp"
#include "SetEnvironmentVariableW.hpp"

T_SetEnvironmentVariableW sys_SetEnvironmentVariableW = nullptr;

/**
 * @brief Detour of SetEnvironmentVariableW().
 *
 * The variable is written to the environment of the sandbox and never to the
 * environment of the host: the block of this process is not touched at all, so
 * the environment of the host keeps the value it had whatever the packaged
 * application does. The modification is handed to the loader, which keeps it in
 * the state directory of the sandbox, so it survives the end of the process.
 *
 * The contract of the call is the one of the operating system: a value of null
 * removes the variable, and a name which is missing or carries an equals sign
 * is refused with `ERROR_INVALID_PARAMETER`.
 */
static BOOL WINAPI Hook_SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue)
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_SetEnvironmentVariableW(lpName, lpValue);
    }

    if (lpName == nullptr || *lpName == L'\0')
    {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (lpValue == nullptr)
    {
        if (!appbox::environment::Isolation::Remove(lpName))
        {
            ::SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return TRUE;
    }

    if (!appbox::environment::Isolation::Store(lpName, lpValue))
    {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return TRUE;
}

static void LoadSetEnvironmentVariableW()
{
    sys_SetEnvironmentVariableW = reinterpret_cast<T_SetEnvironmentVariableW>(
        appbox::environment::ResolveEnvironmentProc("SetEnvironmentVariableW"));
}

appbox::HookRecord appbox::HookSetEnvironmentVariableW = {
    "SetEnvironmentVariableW",
    LoadSetEnvironmentVariableW,
    (void**)&sys_SetEnvironmentVariableW,
    Hook_SetEnvironmentVariableW,
};
