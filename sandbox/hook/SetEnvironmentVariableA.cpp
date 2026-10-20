#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "environment/Isolation.hpp"
#include "SetEnvironmentVariableA.hpp"

T_SetEnvironmentVariableA sys_SetEnvironmentVariableA = nullptr;

/**
 * @brief Detour of SetEnvironmentVariableA().
 *
 * The ANSI entry point writes the environment of the process as well, so the
 * modification is recorded like the one of the wide entry point. The name and
 * the value are recorded in the wide form, which is the form the environment of
 * a process keeps: the value is read back from the block instead of being
 * converted a second time, so the record carries what the operating system
 * stored.
 */
static BOOL WINAPI Hook_SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue)
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_SetEnvironmentVariableA(lpName, lpValue);
    }

    const BOOL stored = sys_SetEnvironmentVariableA(lpName, lpValue);
    if (!stored || lpName == nullptr)
    {
        return stored;
    }

    std::wstring name;
    if (!appbox::environment::AnsiToWide(lpName, name) || name.empty())
    {
        return stored;
    }

    if (lpValue == nullptr)
    {
        /* A value of null removes the variable. */
        appbox::environment::Isolation::Remove(name);
        return stored;
    }

    std::wstring value;
    if (!appbox::environment::AnsiToWide(lpValue, value))
    {
        return stored;
    }

    appbox::environment::Isolation::Store(name, value);
    return stored;
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
