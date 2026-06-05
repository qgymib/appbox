#include "utils/Log.hpp"
#include "NtSetInformationKey.hpp"

T_NtSetInformationKey sys_NtSetInformationKey = nullptr;

static nlohmann::json NtSetInformationKeyLogParam(HANDLE KeyHandle, KEY_SET_INFORMATION_CLASS KeySetInformationClass,
                                                  PVOID KeySetInformation, ULONG KeySetInformationLength)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["KeySetInformationClass"] = KeySetInformationClass;
    param["KeySetInformation"] = appbox::PointerToString(KeySetInformation);
    param["KeySetInformationLength"] = KeySetInformationLength;
    return param;
}

static appbox::LoggerF logger("NtSetInformationKey", NtSetInformationKeyLogParam);

static NTSTATUS Hook_NtSetInformationKey(HANDLE KeyHandle, KEY_SET_INFORMATION_CLASS KeySetInformationClass,
                                         PVOID KeySetInformation, ULONG KeySetInformationLength)
{
    logger.Log(KeyHandle, KeySetInformationClass, KeySetInformation, KeySetInformationLength);
    return sys_NtSetInformationKey(KeyHandle, KeySetInformationClass, KeySetInformation, KeySetInformationLength);
}

static void LoadNtSetInformationKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtSetInformationKey.name);
    sys_NtSetInformationKey = reinterpret_cast<T_NtSetInformationKey>(addr);
}

appbox::HookRecord appbox::HookNtSetInformationKey = {
    "NtSetInformationKey",
    LoadNtSetInformationKey,
    (void**)&sys_NtSetInformationKey,
    Hook_NtSetInformationKey,
};
