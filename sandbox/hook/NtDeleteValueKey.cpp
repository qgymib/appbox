#include "utils/Log.hpp"
#include "NtDeleteValueKey.hpp"

T_NtDeleteValueKey sys_NtDeleteValueKey = nullptr;

static nlohmann::json NtDeleteValueKeyLogParam(HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["ValueName"] = appbox::ToJson(ValueName);
    return param;
}

static appbox::LoggerF logger("NtDeleteValueKey", NtDeleteValueKeyLogParam);

static NTSTATUS Hook_NtDeleteValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    logger.Log(KeyHandle, ValueName);
    return sys_NtDeleteValueKey(KeyHandle, ValueName);
}

static void LoadNtDeleteValueKey()
{
    *appbox::HookNtDeleteValueKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtDeleteValueKey.name);
}

appbox::HookRecord appbox::HookNtDeleteValueKey = {
    "NtDeleteValueKey",
    LoadNtDeleteValueKey,
    (void**)&sys_NtDeleteValueKey,
    Hook_NtDeleteValueKey,
};
