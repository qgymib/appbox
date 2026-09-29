#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "environment/Isolation.hpp"
#include "RtlSetEnvironmentVariable.hpp"

T_RtlSetEnvironmentVariable sys_RtlSetEnvironmentVariable = nullptr;

/**
 * @brief Detour of RtlSetEnvironmentVariable().
 *
 * This is the lowest entry point which writes a variable of the process
 * environment: the variable is written to the environment of the sandbox and
 * never to the block of this process, so the environment of the host keeps the
 * value it had whatever the packaged application does.
 *
 * A caller which passes a block of its own is forwarded: such a call creates or
 * changes a block of the caller and never touches the environment of the
 * process. A name of null replaces the whole environment of the process with
 * the block the value carries, which is the documented behaviour of the call.
 */
static NTSTATUS NTAPI Hook_RtlSetEnvironmentVariable(PWSTR* Environment, PUNICODE_STRING Name, PUNICODE_STRING Value)
{
    if (!appbox::environment::Isolation::IsEnabled() || Environment != nullptr)
    {
        return sys_RtlSetEnvironmentVariable(Environment, Name, Value);
    }

    if (Name == nullptr)
    {
        /* The whole environment is replaced by the block of the caller. */
        if (Value == nullptr || Value->Buffer == nullptr)
        {
            appbox::environment::Isolation::Clear();
            return STATUS_SUCCESS;
        }

        appbox::environment::Isolation::AssignBlock(Value->Buffer);
        return STATUS_SUCCESS;
    }

    std::wstring name;
    if (!appbox::environment::ReadUnicodeStringText(Name, name) || name.empty())
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Value == nullptr)
    {
        /* A value of null removes the variable. */
        if (!appbox::environment::Isolation::Remove(name))
        {
            return STATUS_INVALID_PARAMETER;
        }
        return STATUS_SUCCESS;
    }

    std::wstring value;
    if (!appbox::environment::ReadUnicodeStringText(Value, value))
    {
        /* An empty value is a value which carries no character. */
        value.clear();
    }

    if (!appbox::environment::Isolation::Store(name, value))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

static void LoadRtlSetEnvironmentVariable()
{
    sys_RtlSetEnvironmentVariable =
        reinterpret_cast<T_RtlSetEnvironmentVariable>(GetProcAddress(appbox::sys.h_ntdll, "RtlSetEnvironmentVariable"));
}

appbox::HookRecord appbox::HookRtlSetEnvironmentVariable = {
    "RtlSetEnvironmentVariable",
    LoadRtlSetEnvironmentVariable,
    (void**)&sys_RtlSetEnvironmentVariable,
    Hook_RtlSetEnvironmentVariable,
};
