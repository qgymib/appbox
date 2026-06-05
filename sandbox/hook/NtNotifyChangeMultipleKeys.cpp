#include "utils/Log.hpp"
#include "NtNotifyChangeMultipleKeys.hpp"

T_NtNotifyChangeMultipleKeys sys_NtNotifyChangeMultipleKeys = nullptr;

static nlohmann::json NtNotifyChangeMultipleKeysLogParam(HANDLE MasterKeyHandle, ULONG Count,
                                                         OBJECT_ATTRIBUTES SubordinateObjects[], HANDLE Event,
                                                         PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                                         PIO_STATUS_BLOCK IoStatusBlock, ULONG CompletionFilter,
                                                         BOOLEAN WatchTree, PVOID Buffer, ULONG BufferSize,
                                                         BOOLEAN Asynchronous)
{
    nlohmann::json param;
    param["MasterKeyHandle"] = appbox::PointerToString(MasterKeyHandle);
    param["Count"] = Count;
    for (ULONG i = 0; i < Count; i++)
    {
        auto v = appbox::ToJson(&SubordinateObjects[i]);
        param["SubordinateObjects"].push_back(v);
    }
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

static appbox::LoggerF logger("NtNotifyChangeMultipleKeys", NtNotifyChangeMultipleKeysLogParam);

static NTSTATUS Hook_NtNotifyChangeMultipleKeys(HANDLE MasterKeyHandle, ULONG Count,
                                                OBJECT_ATTRIBUTES SubordinateObjects[], HANDLE Event,
                                                PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                                PIO_STATUS_BLOCK IoStatusBlock, ULONG CompletionFilter,
                                                BOOLEAN WatchTree, PVOID Buffer, ULONG BufferSize, BOOLEAN Asynchronous)
{
    logger.Log(MasterKeyHandle, Count, SubordinateObjects, Event, ApcRoutine, ApcContext, IoStatusBlock,
               CompletionFilter, WatchTree, Buffer, BufferSize, Asynchronous);
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
