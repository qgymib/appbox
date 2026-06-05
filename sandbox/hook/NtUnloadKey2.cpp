#include "utils/Log.hpp"
#include "NtUnloadKey2.hpp"

T_NtUnloadKey2 sys_NtUnloadKey2 = nullptr;

static nlohmann::json NtUnloadKey2LogParam(POBJECT_ATTRIBUTES TargetKey, ULONG Flags)
{
    nlohmann::json param;
    param["TargetKey"] = appbox::ToJson(TargetKey);
    param["Flags"] = Flags;
    return param;
}

static appbox::LoggerF logger("NtUnloadKey2", NtUnloadKey2LogParam);

static NTSTATUS Hook_NtUnloadKey2(POBJECT_ATTRIBUTES TargetKey, ULONG Flags)
{
    logger.Log(TargetKey, Flags);
    return sys_NtUnloadKey2(TargetKey, Flags);
}

static void LoadNtUnloadKey2()
{
    *appbox::HookNtUnloadKey2.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtUnloadKey2.name);
}

appbox::HookRecord appbox::HookNtUnloadKey2 = {
    "NtUnloadKey2",
    LoadNtUnloadKey2,
    (void**)&sys_NtUnloadKey2,
    Hook_NtUnloadKey2,
};
