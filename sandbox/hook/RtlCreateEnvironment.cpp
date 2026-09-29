#include "utils/WinAPI.h" /* Must be first include file */
#include <string>
#include "environment/Isolation.hpp"
#include "RtlCreateEnvironment.hpp"

T_RtlCreateEnvironment sys_RtlCreateEnvironment = nullptr;

/**
 * @brief Detour of RtlCreateEnvironment().
 *
 * A caller which asks for the environment of the process receives the
 * environment of the sandbox: the block of this process holds the values of the
 * host, so copying it would hand the variables of the host to a child process
 * the caller starts with the block.
 *
 * A caller which asks for an empty environment is forwarded, because the block
 * it receives holds no variable of the host either way. The block the sandbox
 * hands out is owned by the caller, which releases it with the entry point
 * which destroys an environment.
 */
static NTSTATUS NTAPI Hook_RtlCreateEnvironment(BOOLEAN Inherit, PWSTR* Environment)
{
    if (!appbox::environment::Isolation::IsEnabled() || !Inherit || Environment == nullptr)
    {
        return sys_RtlCreateEnvironment(Inherit, Environment);
    }

    PWSTR block = appbox::environment::Isolation::CreateOwnedBlock();
    if (block == nullptr)
    {
        return STATUS_NO_MEMORY;
    }

    *Environment = block;
    return STATUS_SUCCESS;
}

static void LoadRtlCreateEnvironment()
{
    sys_RtlCreateEnvironment =
        reinterpret_cast<T_RtlCreateEnvironment>(GetProcAddress(appbox::sys.h_ntdll, "RtlCreateEnvironment"));
}

appbox::HookRecord appbox::HookRtlCreateEnvironment = {
    "RtlCreateEnvironment",
    LoadRtlCreateEnvironment,
    (void**)&sys_RtlCreateEnvironment,
    Hook_RtlCreateEnvironment,
};
