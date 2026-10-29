#include "WinAPI.h" /* Must be first include file */
#include <string>
#include "environment/Isolation.hpp"
#include "SetEnvironmentVariableW.hpp"

T_SetEnvironmentVariableW sys_SetEnvironmentVariableW = nullptr;

/**
 * @brief Detour of SetEnvironmentVariableW().
 *
 * The block of the process is the environment of the sandbox (see
 * `environment/Isolation.cpp`), so the call is forwarded: the operating system
 * writes the value into the block, which is what every reader sees. The
 * modification is recorded afterwards, so it survives the end of the process.
 *
 * The entry point is hooked next to `RtlSetEnvironmentVariable` because the
 * runtime of the operating system does not route every write through the
 * exported reader: `kernelbase` calls an implementation of its own, so a write
 * of the application is only observed here.
 */
static BOOL WINAPI Hook_SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue)
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_SetEnvironmentVariableW(lpName, lpValue);
    }

    const BOOL stored = sys_SetEnvironmentVariableW(lpName, lpValue);
    if (!stored || lpName == nullptr)
    {
        return stored;
    }

    if (lpValue == nullptr)
    {
        /* A value of null removes the variable. */
        appbox::environment::Isolation::Remove(lpName);
        return stored;
    }

    appbox::environment::Isolation::Store(lpName, lpValue);
    return stored;
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
