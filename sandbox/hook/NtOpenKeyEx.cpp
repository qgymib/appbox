#include "utils/Log.hpp"
#include "utils/BitParser.hpp"
#include "hook/NtCreateKey.hpp"
#include "NtOpenKeyEx.hpp"

T_NtOpenKeyEx sys_NtOpenKeyEx = nullptr;

static appbox::BitData s_OpenOptions[] = {
    { "REG_OPTION_OPEN_LINK",      REG_OPTION_OPEN_LINK      },
    { "REG_OPTION_BACKUP_RESTORE", REG_OPTION_BACKUP_RESTORE },
};

static nlohmann::json NtOpenKeyExLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                          POBJECT_ATTRIBUTES ObjectAttributes, ULONG OpenOptions)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["DesiredAccess"] = appbox::RegistryKeyDesiredAccessToJson(DesiredAccess);
    param["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    param["OpenOptions"] = appbox::RegistryKeyOpenOptionsToJson(OpenOptions);
    return param;
}

static appbox::LoggerF logger("NtOpenKeyEx", NtOpenKeyExLogParam);

static NTSTATUS Hook_NtOpenKeyEx(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 ULONG OpenOptions)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions);
    return sys_NtOpenKeyEx(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions);
}

static void LoadNtOpenKeyEx()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtOpenKeyEx");
    sys_NtOpenKeyEx = reinterpret_cast<T_NtOpenKeyEx>(addr);
}

appbox::HookRecord appbox::HookNtOpenKeyEx = {
    "NtOpenKeyEx",
    LoadNtOpenKeyEx,
    (void**)&sys_NtOpenKeyEx,
    Hook_NtOpenKeyEx,
};

nlohmann::json appbox::RegistryKeyOpenOptionsToJson(ULONG OpenOptions)
{
    return appbox::ParseBit(OpenOptions, s_OpenOptions, std::size(s_OpenOptions));
}
