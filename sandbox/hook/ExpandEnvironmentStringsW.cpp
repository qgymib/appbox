#include "utils/WinAPI.h" /* Must be first include file */
#include <cstring>
#include <string>
#include "environment/Isolation.hpp"
#include "ExpandEnvironmentStringsW.hpp"

T_ExpandEnvironmentStringsW sys_ExpandEnvironmentStringsW = nullptr;

/**
 * @brief Detour of ExpandEnvironmentStringsW().
 *
 * The references of the text are expanded from the environment of the sandbox,
 * so a variable an isolation mode hides keeps its spelling and a variable the
 * configuration added is found. Expanding from the environment of this process
 * would report the values of the host instead.
 *
 * The contract of the call is the one of the operating system: the number of
 * characters which were stored, including the terminator, is reported, and a
 * buffer which is too small is left untouched while the size it needs is
 * reported instead.
 */
static DWORD WINAPI Hook_ExpandEnvironmentStringsW(LPCWSTR lpSrc, LPWSTR lpDst, DWORD nSize)
{
    if (!appbox::environment::Isolation::IsEnabled() || lpSrc == nullptr)
    {
        return sys_ExpandEnvironmentStringsW(lpSrc, lpDst, nSize);
    }

    const std::wstring expanded = appbox::environment::Isolation::Expand(lpSrc);

    const DWORD needed = static_cast<DWORD>(expanded.size() + 1);
    if (lpDst == nullptr || nSize < needed)
    {
        return needed;
    }

    std::memcpy(lpDst, expanded.c_str(), static_cast<std::size_t>(needed) * sizeof(wchar_t));
    return needed;
}

static void LoadExpandEnvironmentStringsW()
{
    sys_ExpandEnvironmentStringsW = reinterpret_cast<T_ExpandEnvironmentStringsW>(
        appbox::environment::ResolveEnvironmentProc("ExpandEnvironmentStringsW"));
}

appbox::HookRecord appbox::HookExpandEnvironmentStringsW = {
    "ExpandEnvironmentStringsW",
    LoadExpandEnvironmentStringsW,
    (void**)&sys_ExpandEnvironmentStringsW,
    Hook_ExpandEnvironmentStringsW,
};
