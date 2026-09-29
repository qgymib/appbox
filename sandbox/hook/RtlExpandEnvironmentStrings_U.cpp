#include "utils/WinAPI.h" /* Must be first include file */
#include <cstring>
#include <string>
#include "environment/Isolation.hpp"
#include "RtlExpandEnvironmentStrings_U.hpp"

T_RtlExpandEnvironmentStrings_U sys_RtlExpandEnvironmentStrings_U = nullptr;

/**
 * @brief Detour of RtlExpandEnvironmentStrings_U().
 *
 * The references of the text are expanded from the environment of the sandbox,
 * so the lowest entry point of the operating system reports the same answer as
 * the wide entry point of the process environment and never a value of the host
 * which an isolation mode hides.
 *
 * The contract of the call is the one of the kernel and is measured, not
 * assumed: the reported length counts the bytes of the answer with its
 * terminator, the length of the destination counts the bytes without it, and a
 * destination which is too small reports `STATUS_BUFFER_TOO_SMALL` with the
 * length the caller has to provide.
 */
static NTSTATUS NTAPI Hook_RtlExpandEnvironmentStrings_U(PWSTR Environment, PUNICODE_STRING Source,
                                                         PUNICODE_STRING Destination, PULONG ReturnLength)
{
    if (!appbox::environment::Isolation::IsEnabled() || Environment != nullptr)
    {
        return sys_RtlExpandEnvironmentStrings_U(Environment, Source, Destination, ReturnLength);
    }

    std::wstring source;
    if (!appbox::environment::ReadUnicodeStringText(Source, source))
    {
        return STATUS_INVALID_PARAMETER;
    }

    const std::wstring expanded = appbox::environment::Isolation::Expand(source);
    const ULONG        needed = static_cast<ULONG>((expanded.size() + 1) * sizeof(wchar_t));

    if (ReturnLength != nullptr)
    {
        *ReturnLength = needed;
    }

    if (Destination == nullptr || Destination->Buffer == nullptr || Destination->MaximumLength < needed)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    std::memcpy(Destination->Buffer, expanded.c_str(), needed);
    Destination->Length = static_cast<USHORT>(expanded.size() * sizeof(wchar_t));

    return STATUS_SUCCESS;
}

static void LoadRtlExpandEnvironmentStrings_U()
{
    sys_RtlExpandEnvironmentStrings_U = reinterpret_cast<T_RtlExpandEnvironmentStrings_U>(
        GetProcAddress(appbox::sys.h_ntdll, "RtlExpandEnvironmentStrings_U"));
}

appbox::HookRecord appbox::HookRtlExpandEnvironmentStrings_U = {
    "RtlExpandEnvironmentStrings_U",
    LoadRtlExpandEnvironmentStrings_U,
    (void**)&sys_RtlExpandEnvironmentStrings_U,
    Hook_RtlExpandEnvironmentStrings_U,
};
