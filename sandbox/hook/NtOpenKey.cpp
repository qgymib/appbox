#include "utils/Log.hpp"
#include "hook/NtCreateKey.hpp"
#include "NtOpenKey.hpp"

T_NtOpenKey sys_NtOpenKey = nullptr;

static nlohmann::json NtOpenKeyLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                        POBJECT_ATTRIBUTES ObjectAttributes)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["DesiredAccess"] = appbox::RegistryKeyDesiredAccessToJson(DesiredAccess);
    param["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    return param;
}

static appbox::LoggerF logger("NtOpenKey", NtOpenKeyLogParam);

static NTSTATUS Hook_NtOpenKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes);
    return sys_NtOpenKey(KeyHandle, DesiredAccess, ObjectAttributes);
}

static void LoadNtOpenKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtOpenKey");
    sys_NtOpenKey = reinterpret_cast<T_NtOpenKey>(addr);
}

appbox::HookRecord appbox::HookNtOpenKey = {
    "NtOpenKey",
    LoadNtOpenKey,
    (void**)&sys_NtOpenKey,
    Hook_NtOpenKey,
};
