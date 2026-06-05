#include "utils/Log.hpp"
#include "NtRestoreKey.hpp"

T_NtRestoreKey sys_NtRestoreKey = nullptr;

static nlohmann::json NtRestoreKeyLogParam(HANDLE KeyHandle, HANDLE FileHandle, ULONG Flags)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["Flags"] = Flags;
    return param;
}

static appbox::LoggerF logger("NtRestoreKey", NtRestoreKeyLogParam);

static NTSTATUS Hook_NtRestoreKey(HANDLE KeyHandle, HANDLE FileHandle, ULONG Flags)
{
    logger.Log(KeyHandle, FileHandle, Flags);
    return sys_NtRestoreKey(KeyHandle, FileHandle, Flags);
}

static void LoadNtRestoreKey()
{
    *appbox::HookNtRestoreKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtRestoreKey.name);
}

appbox::HookRecord appbox::HookNtRestoreKey = {
    "NtRestoreKey",
    LoadNtRestoreKey,
    (void**)&sys_NtRestoreKey,
    Hook_NtRestoreKey,
};
