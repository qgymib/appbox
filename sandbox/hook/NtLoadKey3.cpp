#include "utils/Log.hpp"
#include "hook/NtCreateKey.hpp"
#include "NtLoadKey3.hpp"

T_NtLoadKey3 sys_NtLoadKey3 = nullptr;

static nlohmann::json NtLoadKey3LogParam(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile, ULONG Flags,
                                         PVOID ExtendedParameters, ULONG ExtendedParameterCount,
                                         ACCESS_MASK DesiredAccess, PHANDLE RootHandle, PVOID Reserved)
{
    nlohmann::json param;
    param["TargetKey"] = appbox::ToJson(TargetKey);
    param["SourceFile"] = appbox::ToJson(SourceFile);
    param["Flags"] = Flags;
    param["ExtendedParameters"] = appbox::PointerToString(ExtendedParameters);
    param["ExtendedParameterCount"] = ExtendedParameterCount;
    param["DesiredAccess"] = appbox::RegistryKeyDesiredAccessToJson(DesiredAccess);
    param["RootHandle"] = appbox::PointerToString(RootHandle);
    param["Reserved"] = appbox::PointerToString(Reserved);
    return param;
}

static appbox::LoggerF logger("NtLoadKey3", NtLoadKey3LogParam);

static NTSTATUS Hook_NtLoadKey3(POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile, ULONG Flags,
                                PVOID ExtendedParameters, ULONG ExtendedParameterCount, ACCESS_MASK DesiredAccess,
                                PHANDLE RootHandle, PVOID Reserved)
{
    logger.Log(TargetKey, SourceFile, Flags, ExtendedParameters, ExtendedParameterCount, DesiredAccess, RootHandle,
               Reserved);
    return sys_NtLoadKey3(TargetKey, SourceFile, Flags, ExtendedParameters, ExtendedParameterCount, DesiredAccess,
                          RootHandle, Reserved);
}

static void LoadNtLoadKey3()
{
    *appbox::HookNtLoadKey3.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtLoadKey3.name);
}

appbox::HookRecord appbox::HookNtLoadKey3 = {
    "NtLoadKey3",
    LoadNtLoadKey3,
    (void**)&sys_NtLoadKey3,
    Hook_NtLoadKey3,
};
