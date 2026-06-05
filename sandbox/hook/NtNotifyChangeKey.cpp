#include "NtNotifyChangeKey.hpp"

T_NtNotifyChangeKey sys_NtNotifyChangeKey = nullptr;

static NTSTATUS Hook_NtNotifyChangeKey(HANDLE KeyHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                       PIO_STATUS_BLOCK IoStatusBlock, ULONG CompletionFilter, BOOLEAN WatchTree,
                                       PVOID Buffer, ULONG BufferSize, BOOLEAN Asynchronous)
{
    return sys_NtNotifyChangeKey(KeyHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, CompletionFilter, WatchTree,
                                 Buffer, BufferSize, Asynchronous);
}

static void LoadNtNotifyChangeKey()
{
    *appbox::HookNtNotifyChangeKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtNotifyChangeKey.name);
}

appbox::HookRecord appbox::HookNtNotifyChangeKey = {
    "NtNotifyChangeKey",
    LoadNtNotifyChangeKey,
    (void**)&sys_NtNotifyChangeKey,
    Hook_NtNotifyChangeKey,
};
