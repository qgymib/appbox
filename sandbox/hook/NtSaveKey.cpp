#include "utils/Log.hpp"
#include "NtSaveKey.hpp"

T_NtSaveKey sys_NtSaveKey = nullptr;

static nlohmann::json NtSaveKeyLogParam(HANDLE KeyHandle, HANDLE FileHandle)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    return param;
}

static appbox::LoggerF logger("NtSaveKey", NtSaveKeyLogParam);

static NTSTATUS Hook_NtSaveKey(HANDLE KeyHandle, HANDLE FileHandle)
{
    logger.Log(KeyHandle, FileHandle);
    return sys_NtSaveKey(KeyHandle, FileHandle);
}

static void LoadNtSaveKey()
{
    *appbox::HookNtSaveKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtSaveKey.name);
}

appbox::HookRecord appbox::HookNtSaveKey = {
    "NtSaveKey",
    LoadNtSaveKey,
    (void**)&sys_NtSaveKey,
    Hook_NtSaveKey,
};
