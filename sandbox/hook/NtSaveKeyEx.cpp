#include "utils/Log.hpp"
#include "NtSaveKeyEx.hpp"

T_NtSaveKeyEx sys_NtSaveKeyEx = nullptr;

static nlohmann::json NtSaveKeyExLogParam(HANDLE KeyHandle, HANDLE FileHandle, ULONG Format)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["Format"] = Format;
    return param;
}

static appbox::LoggerF logger("NtSaveKeyEx", NtSaveKeyExLogParam);

static NTSTATUS Hook_NtSaveKeyEx(HANDLE KeyHandle, HANDLE FileHandle, ULONG Format)
{
    logger.Log(KeyHandle, FileHandle, Format);
    return sys_NtSaveKeyEx(KeyHandle, FileHandle, Format);
}

static void LoadNtSaveKeyEx()
{
    *appbox::HookNtSaveKeyEx.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtSaveKeyEx.name);
}

appbox::HookRecord appbox::HookNtSaveKeyEx = {
    "NtSaveKeyEx",
    LoadNtSaveKeyEx,
    (void**)&sys_NtSaveKeyEx,
    Hook_NtSaveKeyEx,
};
