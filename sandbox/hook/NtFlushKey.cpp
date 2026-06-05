#include "utils/Log.hpp"
#include "NtFlushKey.hpp"

T_NtFlushKey sys_NtFlushKey = nullptr;

static nlohmann::json NtFlushKeyLogParam(HANDLE KeyHandle)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    return param;
}

static appbox::LoggerF logger("NtFlushKey", NtFlushKeyLogParam);

static NTSTATUS Hook_NtFlushKey(HANDLE KeyHandle)
{
    logger.Log(KeyHandle);
    return sys_NtFlushKey(KeyHandle);
}

static void LoadNtFlushKey()
{
    *appbox::HookNtFlushKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtFlushKey.name);
}

appbox::HookRecord appbox::HookNtFlushKey = {
    "NtFlushKey",
    LoadNtFlushKey,
    (void**)&sys_NtFlushKey,
    Hook_NtFlushKey,
};
