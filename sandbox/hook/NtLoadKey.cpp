#include "utils/Log.hpp"
#include "NtLoadKey.hpp"

T_NtLoadKey sys_NtLoadKey = nullptr;

static nlohmann::json NtLoadKeyLogParam(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile)
{
    nlohmann::json param;
    param["TargetKey"] = appbox::ToJson(TargetKey);
    param["SourceFile"] = appbox::ToJson(SourceFile);
    return param;
}

static appbox::LoggerF logger("NtLoadKey", NtLoadKeyLogParam);

static NTSTATUS Hook_NtLoadKey(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile)
{
    logger.Log(TargetKey, SourceFile);
    return sys_NtLoadKey(TargetKey, SourceFile);
}

static void LoadNtLoadKey()
{
    *appbox::HookNtLoadKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtLoadKey.name);
}

appbox::HookRecord appbox::HookNtLoadKey = {
    "NtLoadKey",
    LoadNtLoadKey,
    (void**)&sys_NtLoadKey,
    Hook_NtLoadKey,
};
