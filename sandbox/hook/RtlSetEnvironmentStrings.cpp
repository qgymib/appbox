#include "utils/WinAPI.h" /* Must be first include file */
#include <cstddef>
#include "environment/Isolation.hpp"
#include "RtlSetEnvironmentStrings.hpp"

T_RtlSetEnvironmentStrings sys_RtlSetEnvironmentStrings = nullptr;

/**
 * @brief Detour of RtlSetEnvironmentStrings().
 *
 * The entry point replaces the whole environment of the process with the block
 * of the caller, which the operating system copies into the heap of the process.
 * The sandbox forwards the call and then records the block, so the environment
 * of the next run carries the replacement like it carries a single variable the
 * application stored.
 *
 * The block is read from the buffer of the caller and bounded by the length the
 * caller declared, because that is the text the operating system copied and
 * because a buffer which is not terminated must not be read past its end.
 *
 * The Win32 entry points `SetEnvironmentStringsW` and `SetEnvironmentStringsA`
 * of `kernelbase` funnel into this export, so a detour of the export observes
 * them as well and neither of them needs a hook of its own.
 */
static NTSTATUS NTAPI Hook_RtlSetEnvironmentStrings(PVOID NewEnvironment, SIZE_T Size)
{
    if (!appbox::environment::Isolation::IsEnabled())
    {
        return sys_RtlSetEnvironmentStrings(NewEnvironment, Size);
    }

    const NTSTATUS status = sys_RtlSetEnvironmentStrings(NewEnvironment, Size);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    appbox::environment::Isolation::AssignBlock(static_cast<const wchar_t*>(NewEnvironment),
                                                static_cast<std::size_t>(Size));

    return status;
}

static void LoadRtlSetEnvironmentStrings()
{
    sys_RtlSetEnvironmentStrings =
        reinterpret_cast<T_RtlSetEnvironmentStrings>(GetProcAddress(appbox::sys.h_ntdll, "RtlSetEnvironmentStrings"));
}

appbox::HookRecord appbox::HookRtlSetEnvironmentStrings = {
    "RtlSetEnvironmentStrings",
    LoadRtlSetEnvironmentStrings,
    (void**)&sys_RtlSetEnvironmentStrings,
    Hook_RtlSetEnvironmentStrings,
};
