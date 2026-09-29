#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "environment/Isolation.hpp"
#include "SetEnvironmentVariableA.hpp"

T_SetEnvironmentVariableA sys_SetEnvironmentVariableA = nullptr;

/**
 * @brief Detour of SetEnvironmentVariableA().
 *
 * The ANSI entry point writes the same environment as the wide one: the name
 * and the value are converted into UTF-16 and the variable is stored in the
 * environment of the sandbox, which never touches the environment of the host.
 */
static BOOL WINAPI Hook_SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue)
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_SetEnvironmentVariableA(lpName, lpValue);
    }

    if (lpName == nullptr || *lpName == '\0')
    {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    std::wstring name;
    if (!appbox::environment::AnsiToWide(std::string(lpName), name))
    {
        ::SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return FALSE;
    }

    if (lpValue == nullptr)
    {
        if (!appbox::environment::Isolation::Remove(name))
        {
            ::SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return TRUE;
    }

    std::wstring value;
    if (!appbox::environment::AnsiToWide(std::string(lpValue), value))
    {
        ::SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return FALSE;
    }

    if (!appbox::environment::Isolation::Store(name, value))
    {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return TRUE;
}

static void LoadSetEnvironmentVariableA()
{
    sys_SetEnvironmentVariableA = reinterpret_cast<T_SetEnvironmentVariableA>(
        appbox::environment::ResolveEnvironmentProc("SetEnvironmentVariableA"));
}

appbox::HookRecord appbox::HookSetEnvironmentVariableA = {
    "SetEnvironmentVariableA",
    LoadSetEnvironmentVariableA,
    (void**)&sys_SetEnvironmentVariableA,
    Hook_SetEnvironmentVariableA,
};
