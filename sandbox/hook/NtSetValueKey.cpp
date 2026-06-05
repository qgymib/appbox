#include "utils/Log.hpp"
#include "NtSetValueKey.hpp"

T_NtSetValueKey sys_NtSetValueKey = nullptr;

static nlohmann::json NtSetValueKeyLogParam(HANDLE KeyHandle, ULONG Index,
                                            KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                            PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["Index"] = Index;
    param["KeyValueInformationClass"] = KeyValueInformationClass;
    param["KeyValueInformation"] = appbox::PointerToString(KeyValueInformation);
    param["Length"] = Length;
    param["ResultLength"] = appbox::PointerToString(ResultLength);
    return param;
}

static appbox::LoggerF logger("NtSetValueKey", NtSetValueKeyLogParam);

static NTSTATUS Hook_NtSetValueKey(HANDLE KeyHandle, ULONG Index, KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                   PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
    return sys_NtSetValueKey(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
}

static void LoadNtSetValueKey()
{
    *appbox::HookNtSetValueKey.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtSetValueKey.name);
}

appbox::HookRecord appbox::HookNtSetValueKey = {
    "NtSetValueKey",
    LoadNtSetValueKey,
    (void**)&sys_NtSetValueKey,
    Hook_NtSetValueKey,
};
