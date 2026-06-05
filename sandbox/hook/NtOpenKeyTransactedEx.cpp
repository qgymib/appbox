#include "utils/Log.hpp"
#include "hook/NtCreateKey.hpp"
#include "hook/NtOpenKeyEx.hpp"
#include "NtOpenKeyTransactedEx.hpp"

T_NtOpenKeyTransactedEx sys_NtOpenKeyTransactedEx = nullptr;

static nlohmann::json NtOpenKeyTransactedExLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                                    POBJECT_ATTRIBUTES ObjectAttributes, ULONG OpenOptions,
                                                    HANDLE TransactionHandle)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["DesiredAccess"] = appbox::RegistryKeyDesiredAccessToJson(DesiredAccess);
    param["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    param["OpenOptions"] = appbox::RegistryKeyOpenOptionsToJson(OpenOptions);
    param["TransactionHandle"] = appbox::PointerToString(TransactionHandle);
    return param;
}

static appbox::LoggerF logger("NtOpenKeyTransactedEx", NtOpenKeyTransactedExLogParam);

static NTSTATUS Hook_NtOpenKeyTransactedEx(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                           POBJECT_ATTRIBUTES ObjectAttributes, ULONG OpenOptions,
                                           HANDLE TransactionHandle)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions, TransactionHandle);
    return sys_NtOpenKeyTransactedEx(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions, TransactionHandle);
}

static void LoadNtOpenKeyTransactedEx()
{
    *appbox::HookNtOpenKeyTransactedEx.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtOpenKeyTransactedEx.name);
}

appbox::HookRecord appbox::HookNtOpenKeyTransactedEx = {
    "NtOpenKeyTransactedEx",
    LoadNtOpenKeyTransactedEx,
    (void**)&sys_NtOpenKeyTransactedEx,
    Hook_NtOpenKeyTransactedEx,
};
