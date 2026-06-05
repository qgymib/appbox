#include "utils/Log.hpp"
#include "NtUnloadKey.hpp"

T_NtUnloadKey sys_NtUnloadKey = nullptr;

static nlohmann::json NtUnloadKeyLogParam(POBJECT_ATTRIBUTES TargetKey)
{
    nlohmann::json param;
    param["TargetKey"] = appbox::ToJson(TargetKey);
    return param;
}

static appbox::LoggerF logger("NtUnloadKey", NtUnloadKeyLogParam);

static NTSTATUS Hook_NtUnloadKey(POBJECT_ATTRIBUTES TargetKey)
{
    logger.Log(TargetKey);
    return sys_NtUnloadKey(TargetKey);
}

static void LoadNtUnloadKey()
{
    *appbox::HookNtUnloadKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtUnloadKey.name);
}

appbox::HookRecord appbox::HookNtUnloadKey = {
    "NtUnloadKey",
    LoadNtUnloadKey,
    (void**)&sys_NtUnloadKey,
    Hook_NtUnloadKey,
};
