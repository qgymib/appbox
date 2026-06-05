#include "utils/Log.hpp"
#include "NtCompressKey.hpp"

T_NtCompressKey sys_NtCompressKey = nullptr;

static nlohmann::json NtCompressKeyLogParam(HANDLE KeyHandle)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    return param;
}

static appbox::LoggerF logger("NtCompressKey", NtCompressKeyLogParam);

static NTSTATUS Hook_NtCompressKey(HANDLE KeyHandle)
{
    logger.Log(KeyHandle);
    return sys_NtCompressKey(KeyHandle);
}

static void LoadNtCompressKey()
{
    *appbox::HookNtCompressKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtCompressKey.name);
}

appbox::HookRecord appbox::HookNtCompressKey = {
    "NtCompressKey",
    LoadNtCompressKey,
    (void**)&sys_NtCompressKey,
    Hook_NtCompressKey,
};
