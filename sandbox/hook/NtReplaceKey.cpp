#include "utils/Log.hpp"
#include "NtReplaceKey.hpp"

T_NtReplaceKey sys_NtReplaceKey = nullptr;

static nlohmann::json NtReplaceKeyLogParam(POBJECT_ATTRIBUTES NewFile, HANDLE TargetHandle, POBJECT_ATTRIBUTES OldFile)
{
    nlohmann::json param;
    param["NewFile"] = appbox::ToJson(NewFile);
    param["TargetHandle"] = appbox::PointerToString(TargetHandle);
    param["OldFile"] = appbox::ToJson(OldFile);
    return param;
}

static appbox::LoggerF logger("NtReplaceKey", NtReplaceKeyLogParam);

static NTSTATUS Hook_NtReplaceKey(POBJECT_ATTRIBUTES NewFile, HANDLE TargetHandle, POBJECT_ATTRIBUTES OldFile)
{
    logger.Log(NewFile, TargetHandle, OldFile);
    return sys_NtReplaceKey(NewFile, TargetHandle, OldFile);
}

static void LoadNtReplaceKey()
{
    *appbox::HookNtReplaceKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtReplaceKey.name);
}

appbox::HookRecord appbox::HookNtReplaceKey = {
    "NtReplaceKey",
    LoadNtReplaceKey,
    (void**)&sys_NtReplaceKey,
    Hook_NtReplaceKey,
};
