#include "utils/Log.hpp"
#include "NtEnumerateValueKey.hpp"

T_NtEnumerateValueKey sys_NtEnumerateValueKey = nullptr;

static nlohmann::json NtEnumerateValueKeyLogParam(HANDLE KeyHandle, ULONG Index,
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

static appbox::LoggerF logger("NtEnumerateValueKey", NtEnumerateValueKeyLogParam);

static NTSTATUS Hook_NtEnumerateValueKey(HANDLE KeyHandle, ULONG Index,
                                         KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                         PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
    return sys_NtEnumerateValueKey(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length,
                                   ResultLength);
}

static void LoadNtEnumerateValueKey()
{
    *appbox::HookNtEnumerateValueKey.ppPointer =
        GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtEnumerateValueKey.name);
}

appbox::HookRecord appbox::HookNtEnumerateValueKey = {
    "NtEnumerateValueKey",
    LoadNtEnumerateValueKey,
    (void**)&sys_NtEnumerateValueKey,
    Hook_NtEnumerateValueKey,
};
