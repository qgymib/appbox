#include "utils/Log.hpp"
#include "NtRenameKey.hpp"

T_NtRenameKey sys_NtRenameKey = nullptr;

static nlohmann::json NtRenameKeyLogParam(HANDLE KeyHandle, PUNICODE_STRING NewName)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["NewName"] = appbox::ToJson(NewName);
    return param;
}

static appbox::LoggerF logger("NtRenameKey", NtRenameKeyLogParam);

static NTSTATUS Hook_NtRenameKey(HANDLE KeyHandle, PUNICODE_STRING NewName)
{
    logger.Log(KeyHandle, NewName);
    return sys_NtRenameKey(KeyHandle, NewName);
}

static void LoadNtRenameKey()
{
    *appbox::HookNtRenameKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtRenameKey.name);
}

appbox::HookRecord appbox::HookNtRenameKey = {
    "NtRenameKey",
    LoadNtRenameKey,
    (void**)&sys_NtRenameKey,
    Hook_NtRenameKey,
};
