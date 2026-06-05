#include "NtQueryOpenSubKeysEx.hpp"

T_NtQueryOpenSubKeysEx sys_NtQueryOpenSubKeysEx = nullptr;

static NTSTATUS Hook_NtQueryOpenSubKeysEx(POBJECT_ATTRIBUTES TargetKey, ULONG BufferLength, PVOID Buffer,
                                          PULONG RequiredSize)
{
    return sys_NtQueryOpenSubKeysEx(TargetKey, BufferLength, Buffer, RequiredSize);
}

static void LoadNtQueryOpenSubKeysEx()
{
    *appbox::HookNtQueryOpenSubKeysEx.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtQueryOpenSubKeysEx.name);
}

appbox::HookRecord appbox::HookNtQueryOpenSubKeysEx = {
    "NtQueryOpenSubKeysEx",
    LoadNtQueryOpenSubKeysEx,
    (void**)&sys_NtQueryOpenSubKeysEx,
    Hook_NtQueryOpenSubKeysEx,
};
