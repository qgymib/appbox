#include "utils/WinAPI.h" /* Must be first include file */
#include <cstring>
#include <string>
#include "environment/Isolation.hpp"
#include "GetEnvironmentVariableW.hpp"

T_GetEnvironmentVariableW sys_GetEnvironmentVariableW = nullptr;

/**
 * @brief Detour of GetEnvironmentVariableW().
 *
 * The variable is answered from the environment of the sandbox, so the value
 * the packaged application reads is the one the isolation composed and never
 * the one the host holds for a variable the isolation hides.
 *
 * The contract of the call is the one of the operating system: the value
 * without its terminator is copied and its length is reported, a buffer which
 * is too small is left untouched and the size it needs is reported instead,
 * and a variable which the environment does not hold reports the failure with
 * `ERROR_ENVVAR_NOT_FOUND`.
 */
static DWORD WINAPI Hook_GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize)
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_GetEnvironmentVariableW(lpName, lpBuffer, nSize);
    }

    if (lpName == nullptr || *lpName == L'\0')
    {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    std::wstring value;
    if (!appbox::environment::Isolation::Query(lpName, value))
    {
        ::SetLastError(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }

    const DWORD needed = static_cast<DWORD>(value.size() + 1);
    if (lpBuffer == nullptr || nSize < needed)
    {
        /* The caller asked for the size; the buffer stays untouched. */
        return needed;
    }

    std::memcpy(lpBuffer, value.c_str(), static_cast<std::size_t>(needed) * sizeof(wchar_t));
    return static_cast<DWORD>(value.size());
}

static void LoadGetEnvironmentVariableW()
{
    sys_GetEnvironmentVariableW = reinterpret_cast<T_GetEnvironmentVariableW>(
        appbox::environment::ResolveEnvironmentProc("GetEnvironmentVariableW"));
}

appbox::HookRecord appbox::HookGetEnvironmentVariableW = {
    "GetEnvironmentVariableW",
    LoadGetEnvironmentVariableW,
    (void**)&sys_GetEnvironmentVariableW,
    Hook_GetEnvironmentVariableW,
};
