#include "utils/Log.hpp"
#include "NtQueryOpenSubKeysEx.hpp"

T_NtQueryOpenSubKeysEx sys_NtQueryOpenSubKeysEx = nullptr;

static nlohmann::json NtQueryOpenSubKeysExLogParam(POBJECT_ATTRIBUTES TargetKey, ULONG BufferLength, PVOID Buffer,
                                                   PULONG RequiredSize)
{
    nlohmann::json param;
    param["TargetKey"] = appbox::ToJson(TargetKey);
    param["BufferLength"] = BufferLength;
    param["Buffer"] = appbox::PointerToString(Buffer);
    param["RequiredSize"] = appbox::PointerToString(RequiredSize);
    return param;
}

static appbox::LoggerF logger("NtQueryOpenSubKeysEx", NtQueryOpenSubKeysExLogParam);

static NTSTATUS Hook_NtQueryOpenSubKeysEx(POBJECT_ATTRIBUTES TargetKey, ULONG BufferLength, PVOID Buffer,
                                          PULONG RequiredSize)
{
    logger.Log(TargetKey, BufferLength, Buffer, RequiredSize);
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
