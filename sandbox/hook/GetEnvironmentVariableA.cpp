#include "utils/WinAPI.h" /* Must be first include file */
#include <cstring>
#include <string>
#include "environment/Isolation.hpp"
#include "GetEnvironmentVariableA.hpp"

T_GetEnvironmentVariableA sys_GetEnvironmentVariableA = nullptr;

/**
 * @brief Detour of GetEnvironmentVariableA().
 *
 * The ANSI entry point reads the same environment as the wide one: the name is
 * converted into UTF-16, the value is answered from the environment of the
 * sandbox and the answer is converted back into the code page of the caller.
 */
static DWORD WINAPI Hook_GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize)
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_GetEnvironmentVariableA(lpName, lpBuffer, nSize);
    }

    if (lpName == nullptr || *lpName == '\0')
    {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    std::wstring name;
    if (!appbox::environment::AnsiToWide(std::string(lpName), name))
    {
        ::SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return 0;
    }

    std::wstring value;
    if (!appbox::environment::Isolation::Query(name, value))
    {
        ::SetLastError(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }

    std::string ansi;
    if (!appbox::environment::WideToAnsi(value, ansi))
    {
        ::SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return 0;
    }

    const DWORD needed = static_cast<DWORD>(ansi.size() + 1);
    if (lpBuffer == nullptr || nSize < needed)
    {
        /* The caller asked for the size; the buffer stays untouched. */
        return needed;
    }

    std::memcpy(lpBuffer, ansi.c_str(), static_cast<std::size_t>(needed));
    return static_cast<DWORD>(ansi.size());
}

static void LoadGetEnvironmentVariableA()
{
    sys_GetEnvironmentVariableA = reinterpret_cast<T_GetEnvironmentVariableA>(
        appbox::environment::ResolveEnvironmentProc("GetEnvironmentVariableA"));
}

appbox::HookRecord appbox::HookGetEnvironmentVariableA = {
    "GetEnvironmentVariableA",
    LoadGetEnvironmentVariableA,
    (void**)&sys_GetEnvironmentVariableA,
    Hook_GetEnvironmentVariableA,
};
