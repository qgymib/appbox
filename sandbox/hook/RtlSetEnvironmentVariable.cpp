#include "WinAPI.h" /* Must be first include file */
#include <string>
#include "environment/Isolation.hpp"
#include "RtlSetEnvironmentVariable.hpp"

T_RtlSetEnvironmentVariable sys_RtlSetEnvironmentVariable = nullptr;

/**
 * @brief Detour of RtlSetEnvironmentVariable().
 *
 * The block of the process is the environment of the sandbox (see
 * `environment/Isolation.cpp`), so the call is forwarded: the operating system
 * writes the value into the block, which is what every reader sees, and the
 * modification is recorded afterwards, so it survives the end of the process.
 *
 * The entry point takes the address of the block it writes. A null address is
 * the environment of the process, and so is the address of the environment
 * pointer of the process parameters: `RtlSetEnvironmentVar` of `ntdll` compares
 * the block against that pointer and takes the lock of the process block when
 * the two match. Both spellings therefore reach the environment of the process
 * and both are recorded. A caller which brings a block of its own is forwarded
 * without a record: such a call changes a block of the caller and never the
 * environment of the process.
 *
 * A name of null is not a call which replaces the whole block: the exported
 * entry point reads the counted name without checking it, so the call faults
 * inside `ntdll` and never returns. The hook therefore records nothing for it.
 *
 * The entry point is hooked next to `SetEnvironmentVariableW` and
 * `SetEnvironmentVariableA`, because the runtime of the operating system does
 * not route every write through the exported entry point: `kernelbase` calls an
 * implementation of its own (`RtlSetEnvironmentVar`), which a detour of the
 * export never observes.
 *
 * A record which cannot be kept is not an error of the call: the block of the
 * process already carries the value, and only the next run would miss it, which
 * the recording reports on its own.
 */
static NTSTATUS NTAPI Hook_RtlSetEnvironmentVariable(PWSTR* Environment, PUNICODE_STRING Name, PUNICODE_STRING Value)
{
    if (!appbox::environment::Isolation::IsEnabled() || !appbox::environment::IsProcessEnvironment(Environment))
    {
        return sys_RtlSetEnvironmentVariable(Environment, Name, Value);
    }

    const NTSTATUS status = sys_RtlSetEnvironmentVariable(Environment, Name, Value);
    if (!NT_SUCCESS(status) || Name == nullptr)
    {
        return status;
    }

    std::wstring name;
    if (!appbox::environment::ReadUnicodeStringText(Name, name) || name.empty())
    {
        /* The name is not usable, so the modification is not recorded. */
        return status;
    }

    if (Value == nullptr)
    {
        /* A value of null removes the variable. */
        appbox::environment::Isolation::Remove(name);
        return status;
    }

    std::wstring value;
    if (!appbox::environment::ReadUnicodeStringText(Value, value))
    {
        /* An empty value is a value which carries no character. */
        value.clear();
    }

    appbox::environment::Isolation::Store(name, value);
    return status;
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
