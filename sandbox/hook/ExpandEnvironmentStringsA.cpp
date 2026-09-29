#include "utils/WinAPI.h" /* Must be first include file */
#include <cstring>
#include <string>
#include "environment/Isolation.hpp"
#include "ExpandEnvironmentStringsA.hpp"

T_ExpandEnvironmentStringsA sys_ExpandEnvironmentStringsA = nullptr;

/**
 * @brief Detour of ExpandEnvironmentStringsA().
 *
 * The text of the caller is converted into UTF-16, its references are expanded
 * from the environment of the sandbox and the answer is converted back into the
 * code page of the caller.
 */
static DWORD WINAPI Hook_ExpandEnvironmentStringsA(LPCSTR lpSrc, LPSTR lpDst, DWORD nSize)
{
    if (!appbox::environment::Isolation::IsEnabled() || lpSrc == nullptr)
    {
        return sys_ExpandEnvironmentStringsA(lpSrc, lpDst, nSize);
    }

    std::wstring source;
    if (!appbox::environment::AnsiToWide(std::string(lpSrc), source))
    {
        ::SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return 0;
    }

    const std::wstring expanded = appbox::environment::Isolation::Expand(source);

    std::string ansi;
    if (!appbox::environment::WideToAnsi(expanded, ansi))
    {
        ::SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return 0;
    }

    const DWORD needed = static_cast<DWORD>(ansi.size() + 1);
    if (lpDst == nullptr || nSize < needed)
    {
        return needed;
    }

    std::memcpy(lpDst, ansi.c_str(), static_cast<std::size_t>(needed));
    return needed;
}

static void LoadExpandEnvironmentStringsA()
{
    sys_ExpandEnvironmentStringsA = reinterpret_cast<T_ExpandEnvironmentStringsA>(
        appbox::environment::ResolveEnvironmentProc("ExpandEnvironmentStringsA"));
}

appbox::HookRecord appbox::HookExpandEnvironmentStringsA = {
    "ExpandEnvironmentStringsA",
    LoadExpandEnvironmentStringsA,
    (void**)&sys_ExpandEnvironmentStringsA,
    Hook_ExpandEnvironmentStringsA,
};
