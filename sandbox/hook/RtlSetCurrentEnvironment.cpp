#include "WinAPI.h" /* Must be first include file */
#include "environment/Isolation.hpp"
#include "RtlSetCurrentEnvironment.hpp"

T_RtlSetCurrentEnvironment sys_RtlSetCurrentEnvironment = nullptr;

/**
 * @brief Detour of RtlSetCurrentEnvironment().
 *
 * The entry point installs a block of the caller as the environment of the
 * process and releases the block it replaces, so it is the second way an
 * application replaces its whole environment. The sandbox forwards the call and
 * records the block, so the replacement survives the end of the process like the
 * variables an application stores one by one.
 *
 * The installation of the composed environment uses the same entry point while
 * the sandbox is initialized (`environment/Isolation.cpp`, `InstallBlock`). That
 * call runs before the hooks are attached — the module list of `Sandbox.cpp`
 * initializes the environment before `InitHook` — so the detour never observes
 * it and the composed environment is not recorded as a modification of the
 * application.
 */
static NTSTATUS NTAPI Hook_RtlSetCurrentEnvironment(PWSTR Environment, PWSTR* PreviousEnvironment)
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_RtlSetCurrentEnvironment(Environment, PreviousEnvironment);
    }

    const NTSTATUS status = sys_RtlSetCurrentEnvironment(Environment, PreviousEnvironment);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    appbox::environment::Isolation::AssignBlock(Environment);

    return status;
}

static void LoadRtlSetCurrentEnvironment()
{
    sys_RtlSetCurrentEnvironment =
        reinterpret_cast<T_RtlSetCurrentEnvironment>(GetProcAddress(appbox::sys.h_ntdll, "RtlSetCurrentEnvironment"));
}

appbox::HookRecord appbox::HookRtlSetCurrentEnvironment = {
    "RtlSetCurrentEnvironment",
    LoadRtlSetCurrentEnvironment,
    (void**)&sys_RtlSetCurrentEnvironment,
    Hook_RtlSetCurrentEnvironment,
};
