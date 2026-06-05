#include "utils/Log.hpp"
#include "hook/NtCreateKey.hpp"
#include "NtCreateKeyTransacted.hpp"

T_NtCreateKeyTransacted sys_NtCreateKeyTransacted = nullptr;

static nlohmann::json NtCreateKeyTransactedLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                                    POBJECT_ATTRIBUTES ObjectAttributes, ULONG TitleIndex,
                                                    PCUNICODE_STRING Class, ULONG CreateOptions,
                                                    HANDLE TransactionHandle, PULONG Disposition)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["DesiredAccess"] = appbox::RegistryKeyDesiredAccessToJson(DesiredAccess);
    param["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    param["TitleIndex"] = TitleIndex;
    param["Class"] = appbox::ToJson(Class);
    param["CreateOptions"] = appbox::RegistryKeyCreateOptionsToJson(CreateOptions);
    param["TransactionHandle"] = appbox::PointerToString(TransactionHandle);
    param["Disposition"] = appbox::PointerToString(Disposition);
    return param;
}

static appbox::LoggerF logger("NtCreateKeyTransacted", NtCreateKeyTransactedLogParam);

static NTSTATUS Hook_NtCreateKeyTransacted(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                           POBJECT_ATTRIBUTES ObjectAttributes, ULONG TitleIndex,
                                           PCUNICODE_STRING Class, ULONG CreateOptions, HANDLE TransactionHandle,
                                           PULONG Disposition)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions, TransactionHandle,
               Disposition);
    return sys_NtCreateKeyTransacted(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions,
                                     TransactionHandle, Disposition);
}

static void LoadNtCreateKeyTransacted()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtCreateKeyTransacted");
    sys_NtCreateKeyTransacted = reinterpret_cast<T_NtCreateKeyTransacted>(addr);
}

appbox::HookRecord appbox::HookNtCreateKeyTransacted = {
    "NtCreateKeyTransacted",
    LoadNtCreateKeyTransacted,
    (void**)&sys_NtCreateKeyTransacted,
    Hook_NtCreateKeyTransacted,
};
