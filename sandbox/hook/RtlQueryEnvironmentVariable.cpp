#include "utils/WinAPI.h" /* Must be first include file */
#include <cstring>
#include <string>
#include "environment/Isolation.hpp"
#include "RtlQueryEnvironmentVariable.hpp"

T_RtlQueryEnvironmentVariable sys_RtlQueryEnvironmentVariable = nullptr;

/**
 * @brief Detour of RtlQueryEnvironmentVariable().
 *
 * The counted variant of the reader of the process environment is answered from
 * the environment of the sandbox as well, so an application which uses the
 * lowest entry point of the operating system sees the same variables as one
 * which uses the wide entry point of the process environment.
 *
 * The contract of the call is the one of the kernel and is measured, not
 * assumed: the name and the size of the value buffer are counted in characters,
 * the value is copied with its terminator, and the reported length is counted
 * in characters as well — the length of the value while the call succeeds and
 * the length with the terminator while the buffer is too small. A variable
 * which the environment does not hold reports `STATUS_VARIABLE_NOT_FOUND`.
 */
static NTSTATUS NTAPI Hook_RtlQueryEnvironmentVariable(PWSTR Environment, PWSTR Name, SIZE_T NameLength, PWSTR Value,
                                                       SIZE_T ValueLength, PSIZE_T ReturnLength)
{
    if (!appbox::environment::Isolation::IsEnabled() || Environment != nullptr)
    {
        return sys_RtlQueryEnvironmentVariable(Environment, Name, NameLength, Value, ValueLength, ReturnLength);
    }

    if (Name == nullptr || NameLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* The name is counted and is not required to be terminated. */
    const std::wstring name(Name, NameLength);

    std::wstring value;
    if (!appbox::environment::Isolation::Query(name, value))
    {
        return STATUS_VARIABLE_NOT_FOUND;
    }

    /* The size the caller needs, including the terminator. */
    const SIZE_T needed = value.size() + 1;
    if (ReturnLength != nullptr)
    {
        *ReturnLength = needed;
    }

    if (Value == nullptr || ValueLength < needed)
    {
        /* The caller brought no buffer or a buffer which is too small. */
        return STATUS_BUFFER_TOO_SMALL;
    }

    std::memcpy(Value, value.c_str(), needed * sizeof(wchar_t));

    if (ReturnLength != nullptr)
    {
        /* The call succeeded, so the reported length carries no terminator. */
        *ReturnLength = value.size();
    }

    return STATUS_SUCCESS;
}

static void LoadRtlQueryEnvironmentVariable()
{
    sys_RtlQueryEnvironmentVariable = reinterpret_cast<T_RtlQueryEnvironmentVariable>(
        GetProcAddress(appbox::sys.h_ntdll, "RtlQueryEnvironmentVariable"));
}

appbox::HookRecord appbox::HookRtlQueryEnvironmentVariable = {
    "RtlQueryEnvironmentVariable",
    LoadRtlQueryEnvironmentVariable,
    (void**)&sys_RtlQueryEnvironmentVariable,
    Hook_RtlQueryEnvironmentVariable,
};
