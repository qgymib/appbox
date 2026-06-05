#include "utils/Log.hpp"
#include "NtCompactKeys.hpp"

T_NtCompactKeys sys_NtCompactKeys = nullptr;

static nlohmann::json NtCompactKeysLogParam(ULONG Count, HANDLE KeyArray[])
{
    nlohmann::json param;
    param["Count"] = Count;

    for (ULONG i = 0; i < Count; i++)
    {
        auto s = appbox::PointerToString(KeyArray[i]);
        param["KeyArray"].push_back(s);
    }

    return param;
}

static appbox::LoggerF logger("NtCompactKeys", NtCompactKeysLogParam);

static NTSTATUS Hook_NtCompactKeys(ULONG Count, HANDLE KeyArray[])
{
    logger.Log(Count, KeyArray);
    return sys_NtCompactKeys(Count, KeyArray);
}

static void LoadNtCompactKeys()
{
    *appbox::HookNtCompactKeys.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtCompactKeys.name);
}

appbox::HookRecord appbox::HookNtCompactKeys = {
    "NtCompactKeys",
    LoadNtCompactKeys,
    (void**)&sys_NtCompactKeys,
    Hook_NtCompactKeys,
};
