#include "utils/Log.hpp"
#include "NtLockRegistryKey.hpp"

T_NtLockRegistryKey sys_NtLockRegistryKey = nullptr;

static nlohmann::json NtLockRegistryKeyLogParam(HANDLE KeyHandle)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    return param;
}

static appbox::LoggerF logger("NtLockRegistryKey", NtLockRegistryKeyLogParam);

static NTSTATUS Hook_NtLockRegistryKey(HANDLE KeyHandle)
{
    logger.Log(KeyHandle);
    return sys_NtLockRegistryKey(KeyHandle);
}

static void LoadNtLockRegistryKey()
{
    *appbox::HookNtLockRegistryKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtLockRegistryKey.name);
}

appbox::HookRecord appbox::HookNtLockRegistryKey = {
    "NtLockRegistryKey",
    LoadNtLockRegistryKey,
    (void**)&sys_NtLockRegistryKey,
    Hook_NtLockRegistryKey,
};
