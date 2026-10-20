#include "utils/WinAPI.h" /* Must be first include file */
#include <cstring>
#include <string>
#include "environment/Isolation.hpp"
#include "GetTempPathW.hpp"

T_GetTempPathW sys_GetTempPathW = nullptr;

/**
 * @brief Detour of GetTempPathW().
 *
 * The temporary directory is read from the environment of the sandbox and not
 * from the implementation of the operating system: that implementation asks the
 * lowest reader of the environment for the value, and a detour of that reader
 * breaks the compatibility query of `CreateProcessInternalW` on the 32 bit
 * architecture — the query reads the temporary directory of the process while
 * it creates a child process, and the frame of `AslPathIsTemporaryDirectory`
 * loses the path it keeps in a register across the call, which makes it compare
 * the temporary directory against a null pointer. Answering the call here keeps
 * the value of the sandbox and keeps the query away from the detour.
 *
 * The call follows the contract of the operating system: `TMP`, `TEMP` and
 * `USERPROFILE` are tried in that order, a directory which the environment does
 * not hold is skipped, the answer ends with a separator, and a buffer which is
 * too small reports the size the caller has to provide including the
 * terminator. The answer of the sandbox is authoritative, so the check of the
 * operating system whether the directory exists is not repeated; only an
 * environment which holds none of the three variables is forwarded.
 */
static DWORD WINAPI Hook_GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer)
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_GetTempPathW(nBufferLength, lpBuffer);
    }

    static const wchar_t* s_names[] = { L"TMP", L"TEMP", L"USERPROFILE" };

    std::wstring path;
    for (const wchar_t* name : s_names)
    {
        std::wstring value;
        if (appbox::environment::Isolation::Query(name, value) && !value.empty())
        {
            path = value;
            break;
        }
    }

    if (path.empty())
    {
        /* The environment holds none of the variables which name a directory. */
        return sys_GetTempPathW(nBufferLength, lpBuffer);
    }

    /* The answer of the operating system ends with a separator. */
    if (path.back() != L'\\')
    {
        path.push_back(L'\\');
    }

    const DWORD needed = static_cast<DWORD>(path.size() + 1);
    if (lpBuffer == nullptr || nBufferLength < needed)
    {
        return needed;
    }

    std::memcpy(lpBuffer, path.c_str(), needed * sizeof(wchar_t));

    return static_cast<DWORD>(path.size());
}

static void LoadGetTempPathW()
{
    sys_GetTempPathW = reinterpret_cast<T_GetTempPathW>(GetProcAddress(appbox::sys.h_kernelbase, "GetTempPathW"));
}

appbox::HookRecord appbox::HookGetTempPathW = {
    "GetTempPathW",
    LoadGetTempPathW,
    (void**)&sys_GetTempPathW,
    Hook_GetTempPathW,
};
