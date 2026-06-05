#include "utils/Log.hpp"
#include "NtDeleteKey.hpp"

T_NtDeleteKey sys_NtDeleteKey = nullptr;

static nlohmann::json NtDeleteKeyLogParam(HANDLE KeyHandle)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    return param;
}

static appbox::LoggerF logger("NtDeleteKey", NtDeleteKeyLogParam);

static NTSTATUS Hook_NtDeleteKey(HANDLE KeyHandle)
{
    logger.Log(KeyHandle);
    return sys_NtDeleteKey(KeyHandle);
}

static void LoadNtDeleteKey()
{
    *appbox::HookNtDeleteKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtDeleteKey.name);
}

appbox::HookRecord appbox::HookNtDeleteKey = {
    "NtDeleteKey",
    LoadNtDeleteKey,
    (void**)&sys_NtDeleteKey,
    Hook_NtDeleteKey,
};
