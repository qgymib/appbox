#include "NtNotifyChangeMultipleKeys.hpp"

T_NtNotifyChangeMultipleKeys sys_NtNotifyChangeMultipleKeys = nullptr;

static NTSTATUS Hook_NtNotifyChangeMultipleKeys(HANDLE MasterKeyHandle, ULONG Count,
                                                OBJECT_ATTRIBUTES SubordinateObjects[], HANDLE Event,
                                                PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                                PIO_STATUS_BLOCK IoStatusBlock, ULONG CompletionFilter,
                                                BOOLEAN WatchTree, PVOID Buffer, ULONG BufferSize, BOOLEAN Asynchronous)
{
    return sys_NtNotifyChangeMultipleKeys(MasterKeyHandle, Count, SubordinateObjects, Event, ApcRoutine, ApcContext,
                                          IoStatusBlock, CompletionFilter, WatchTree, Buffer, BufferSize, Asynchronous);
}

static void LoadNtNotifyChangeMultipleKeys()
{
    *appbox::HookNtNotifyChangeMultipleKeys.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtNotifyChangeMultipleKeys.name);
}

appbox::HookRecord appbox::HookNtNotifyChangeMultipleKeys = {
    "NtNotifyChangeMultipleKeys",
    LoadNtNotifyChangeMultipleKeys,
    (void**)&sys_NtNotifyChangeMultipleKeys,
    Hook_NtNotifyChangeMultipleKeys,
};
