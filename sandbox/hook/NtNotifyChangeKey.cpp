#include "utils/Log.hpp"
#include "NtNotifyChangeKey.hpp"

T_NtNotifyChangeKey sys_NtNotifyChangeKey = nullptr;

static nlohmann::json NtNotifyChangeKeyLogParam(HANDLE KeyHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                                PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock,
                                                ULONG CompletionFilter, BOOLEAN WatchTree, PVOID Buffer,
                                                ULONG BufferSize, BOOLEAN Asynchronous)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["Event"] = appbox::PointerToString(Event);
    param["ApcRoutine"] = appbox::PointerToString(ApcRoutine);
    param["ApcContext"] = appbox::PointerToString(ApcContext);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["CompletionFilter"] = CompletionFilter;
    param["WatchTree"] = WatchTree;
    param["Buffer"] = appbox::PointerToString(Buffer);
    param["BufferSize"] = BufferSize;
    param["Asynchronous"] = Asynchronous;
    return param;
}

static appbox::LoggerF logger("NtNotifyChangeKey", NtNotifyChangeKeyLogParam);

static NTSTATUS Hook_NtNotifyChangeKey(HANDLE KeyHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                       PIO_STATUS_BLOCK IoStatusBlock, ULONG CompletionFilter, BOOLEAN WatchTree,
                                       PVOID Buffer, ULONG BufferSize, BOOLEAN Asynchronous)
{
    logger.Log(KeyHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, CompletionFilter, WatchTree, Buffer, BufferSize,
               Asynchronous);
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
