#include "utils/Log.hpp"
#include "hook/NtCreateKey.hpp"
#include "NtOpenKeyTransacted.hpp"

T_NtOpenKeyTransacted sys_NtOpenKeyTransacted = nullptr;

static nlohmann::json NtOpenKeyTransactedLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                                  POBJECT_ATTRIBUTES ObjectAttributes, HANDLE TransactionHandle)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["DesiredAccess"] = appbox::RegistryKeyDesiredAccessToJson(DesiredAccess);
    param["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    param["TransactionHandle"] = appbox::PointerToString(TransactionHandle);
    return param;
}

static appbox::LoggerF logger("NtOpenKeyTransacted", NtOpenKeyTransactedLogParam);

static NTSTATUS Hook_NtOpenKeyTransacted(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                         POBJECT_ATTRIBUTES ObjectAttributes, HANDLE TransactionHandle)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, TransactionHandle);
    return sys_NtOpenKeyTransacted(KeyHandle, DesiredAccess, ObjectAttributes, TransactionHandle);
}

static void LoadNtOpenKeyTransacted()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtOpenKeyTransacted");
    sys_NtOpenKeyTransacted = reinterpret_cast<T_NtOpenKeyTransacted>(addr);
}

appbox::HookRecord appbox::HookNtOpenKeyTransacted = {
    "NtOpenKeyTransacted",
    LoadNtOpenKeyTransacted,
    (void**)&sys_NtOpenKeyTransacted,
    Hook_NtOpenKeyTransacted,
};
