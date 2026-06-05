#include "utils/Log.hpp"
#include "NtQueryOpenSubKeys.hpp"

T_NtQueryOpenSubKeys sys_NtQueryOpenSubKeys = nullptr;

static nlohmann::json NtQueryOpenSubKeysLogParam(POBJECT_ATTRIBUTES TargetKey, PULONG HandleCount)
{
    nlohmann::json param;
    param["TargetKey"] = appbox::ToJson(TargetKey);
    param["HandleCount"] = appbox::PointerToString(HandleCount);
    return param;
}

static appbox::LoggerF logger("NtQueryOpenSubKeys", NtQueryOpenSubKeysLogParam);

static NTSTATUS Hook_NtQueryOpenSubKeys(POBJECT_ATTRIBUTES TargetKey, PULONG HandleCount)
{
    logger.Log(TargetKey, HandleCount);
    return sys_NtQueryOpenSubKeys(TargetKey, HandleCount);
}

static void LoadNtQueryOpenSubKeys()
{
    *appbox::HookNtQueryOpenSubKeys.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtQueryOpenSubKeys.name);
}

appbox::HookRecord appbox::HookNtQueryOpenSubKeys = {
    "NtQueryOpenSubKeys",
    LoadNtQueryOpenSubKeys,
    (void**)&sys_NtQueryOpenSubKeys,
    Hook_NtQueryOpenSubKeys,
};
