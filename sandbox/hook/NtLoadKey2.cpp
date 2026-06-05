#include "utils/Log.hpp"
#include "NtLoadKey2.hpp"

T_NtLoadKey2 sys_NtLoadKey2 = nullptr;

static nlohmann::json NtLoadKey2LogParam(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile, ULONG Flags)
{
    nlohmann::json param;
    param["TargetKey"] = appbox::ToJson(TargetKey);
    param["SourceFile"] = appbox::ToJson(SourceFile);
    param["Flags"] = Flags;
    return param;
}

static appbox::LoggerF logger("NtLoadKey2", NtLoadKey2LogParam);

static NTSTATUS Hook_NtLoadKey2(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile, ULONG Flags)
{
    logger.Log(TargetKey, SourceFile, Flags);
    return sys_NtLoadKey2(TargetKey, SourceFile, Flags);
}

static void LoadNtLoadKey2()
{
    *appbox::HookNtLoadKey2.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtLoadKey2.name);
}

appbox::HookRecord appbox::HookNtLoadKey2 = {
    "NtLoadKey2",
    LoadNtLoadKey2,
    (void**)&sys_NtLoadKey2,
    Hook_NtLoadKey2,
};
