#include "utils/Log.hpp"
#include "NtOpenSymbolicLinkObject.hpp"

T_NtOpenSymbolicLinkObject sys_NtOpenSymbolicLinkObject = nullptr;

static nlohmann::json NtOpenSymbolicLinkObjectLogParam(PHANDLE LinkHandle, ACCESS_MASK DesiredAccess,
                                                       POBJECT_ATTRIBUTES ObjectAttributes)
{
    nlohmann::json param;
    param["LinkHandle"] = appbox::PointerToString(LinkHandle);
    param["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    param["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    return param;
}

static appbox::LoggerF logger("NtOpenSymbolicLinkObject", NtOpenSymbolicLinkObjectLogParam);

static NTSTATUS Hook_NtOpenSymbolicLinkObject(PHANDLE LinkHandle, ACCESS_MASK DesiredAccess,
                                              POBJECT_ATTRIBUTES ObjectAttributes)
{
    logger.Log(LinkHandle, DesiredAccess, ObjectAttributes);
    return sys_NtOpenSymbolicLinkObject(LinkHandle, DesiredAccess, ObjectAttributes);
}

static void LoadNtOpenSymbolicLinkObject()
{
    *appbox::HookNtOpenSymbolicLinkObject.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtOpenSymbolicLinkObject.name);
}

appbox::HookRecord appbox::HookNtOpenSymbolicLinkObject = {
    "NtOpenSymbolicLinkObject",
    LoadNtOpenSymbolicLinkObject,
    (void**)&sys_NtOpenSymbolicLinkObject,
    Hook_NtOpenSymbolicLinkObject,
};
