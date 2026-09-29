#include "utils/WinAPI.h" /* Must be first include file */
#include <cstring>
#include <string>
#include "environment/Isolation.hpp"
#include "RtlQueryEnvironmentVariable_U.hpp"

T_RtlQueryEnvironmentVariable_U sys_RtlQueryEnvironmentVariable_U = nullptr;

/**
 * @brief Detour of RtlQueryEnvironmentVariable_U().
 *
 * This is the lowest entry point which reads a variable of the process
 * environment: every other reader of the process environment ends up in the
 * block of the process, which the sandbox never touches, so the variable is
 * answered here from the environment of the sandbox. A caller which brings a
 * block of its own is forwarded, because such a block is memory of the caller
 * and not the environment of the process.
 *
 * The contract of the call is the one of the kernel and is measured, not
 * assumed: the value is copied without its terminator, its length in bytes is
 * reported through the length of the value, a caller which brings no buffer —
 * either no value structure at all or one whose buffer is null — and a buffer
 * which is too small both report `STATUS_BUFFER_TOO_SMALL` and write the size
 * the caller has to provide into the length of the value, and a variable which
 * the environment does not hold reports `STATUS_VARIABLE_NOT_FOUND`.
 *
 * A caller which brings no buffer must never be answered with success: the
 * loader of the operating system asks for the search path of a DLL that way and
 * reads the length of the value afterwards, so a success would make it copy
 * from a null buffer.
 */
static NTSTATUS NTAPI Hook_RtlQueryEnvironmentVariable_U(PWSTR Environment, PUNICODE_STRING Name, PUNICODE_STRING Value,
                                                         PULONG ReturnLength)
{
    if (!appbox::environment::Isolation::IsEnabled() || Environment != nullptr)
    {
        return sys_RtlQueryEnvironmentVariable_U(Environment, Name, Value, ReturnLength);
    }

    std::wstring name;
    if (!appbox::environment::ReadUnicodeStringText(Name, name) || name.empty())
    {
        return STATUS_INVALID_PARAMETER;
    }

    std::wstring value;
    if (!appbox::environment::Isolation::Query(name, value))
    {
        return STATUS_VARIABLE_NOT_FOUND;
    }

    const ULONG needed = static_cast<ULONG>(value.size() * sizeof(wchar_t));

    if (Value == nullptr)
    {
        /* The caller brought no value at all, so it is told how much it needs. */
        if (ReturnLength != nullptr)
        {
            *ReturnLength = needed;
        }
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Value->Buffer == nullptr || Value->MaximumLength < needed)
    {
        /* The length of the value carries the size the caller has to provide. */
        Value->Length = static_cast<USHORT>(needed);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (needed > 0)
    {
        std::memcpy(Value->Buffer, value.data(), needed);
    }
    Value->Length = static_cast<USHORT>(needed);

    return STATUS_SUCCESS;
}

static void LoadRtlQueryEnvironmentVariable_U()
{
    sys_RtlQueryEnvironmentVariable_U = reinterpret_cast<T_RtlQueryEnvironmentVariable_U>(
        GetProcAddress(appbox::sys.h_ntdll, "RtlQueryEnvironmentVariable_U"));
}

appbox::HookRecord appbox::HookRtlQueryEnvironmentVariable_U = {
    "RtlQueryEnvironmentVariable_U",
    LoadRtlQueryEnvironmentVariable_U,
    (void**)&sys_RtlQueryEnvironmentVariable_U,
    Hook_RtlQueryEnvironmentVariable_U,
};
