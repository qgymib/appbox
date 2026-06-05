#include "utils/Log.hpp"
#include "NtEnumerateKey.hpp"

T_NtEnumerateKey sys_NtEnumerateKey = nullptr;

static nlohmann::json NtEnumerateKeyLogParam(HANDLE KeyHandle, ULONG Index, KEY_INFORMATION_CLASS KeyInformationClass,
                                             PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    nlohmann::json param;
    param["KeyHandle"] = appbox::PointerToString(KeyHandle);
    param["Index"] = Index;
    param["KeyInformationClass"] = KeyInformationClass;
    param["KeyInformation"] = appbox::PointerToString(KeyInformation);
    param["Length"] = Length;
    param["ResultLength"] = appbox::PointerToString(ResultLength);
    return param;
}

static appbox::LoggerF logger("NtEnumerateKey", NtEnumerateKeyLogParam);

static NTSTATUS Hook_NtEnumerateKey(HANDLE KeyHandle, ULONG Index, KEY_INFORMATION_CLASS KeyInformationClass,
                                    PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);
    return sys_NtEnumerateKey(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);
}

static void LoadNtEnumerateKey()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtEnumerateKey");
    sys_NtEnumerateKey = reinterpret_cast<T_NtEnumerateKey>(addr);
}

appbox::HookRecord appbox::HookNtEnumerateKey = {
    "NtEnumerateKey",
    LoadNtEnumerateKey,
    (void**)&sys_NtEnumerateKey,
    Hook_NtEnumerateKey,
};
