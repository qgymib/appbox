#include "utils/Log.hpp"
#include "hook/NtCreateKey.hpp"
#include "NtLoadKeyEx.hpp"

T_NtLoadKeyEx sys_NtLoadKeyEx = nullptr;

static nlohmann::json NtLoadKeyExLogParam(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile, ULONG Flags,
                                          HANDLE TrustClassKey, HANDLE Event, ACCESS_MASK DesiredAccess,
                                          PHANDLE RootHandle, PVOID Reserved)
{
    nlohmann::json param;
    param["TargetKey"] = appbox::ToJson(TargetKey);
    param["SourceFile"] = appbox::ToJson(SourceFile);
    param["Flags"] = Flags;
    param["TrustClassKey"] = appbox::PointerToString(TrustClassKey);
    param["Event"] = appbox::PointerToString(Event);
    param["DesiredAccess"] = appbox::RegistryKeyDesiredAccessToJson(DesiredAccess);
    param["RootHandle"] = appbox::PointerToString(RootHandle);
    param["Reserved"] = appbox::PointerToString(Reserved);
    return param;
}

static appbox::LoggerF logger("NtLoadKeyEx", NtLoadKeyExLogParam);

static NTSTATUS Hook_NtLoadKeyEx(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile, ULONG Flags,
                                 HANDLE TrustClassKey, HANDLE Event, ACCESS_MASK DesiredAccess, PHANDLE RootHandle,
                                 PVOID Reserved)
{
    logger.Log(TargetKey, SourceFile, Flags, TrustClassKey, Event, DesiredAccess, RootHandle, Reserved);
    return sys_NtLoadKeyEx(TargetKey, SourceFile, Flags, TrustClassKey, Event, DesiredAccess, RootHandle, Reserved);
}

static void LoadNtLoadKeyEx()
{
    *appbox::HookNtLoadKeyEx.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtLoadKeyEx.name);
}

appbox::HookRecord appbox::HookNtLoadKeyEx = {
    "NtLoadKeyEx",
    LoadNtLoadKeyEx,
    (void**)&sys_NtLoadKeyEx,
    Hook_NtLoadKeyEx,
};
