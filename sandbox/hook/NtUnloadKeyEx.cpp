#include "utils/Log.hpp"
#include "NtUnloadKeyEx.hpp"

T_NtUnloadKeyEx sys_NtUnloadKeyEx = nullptr;

static nlohmann::json NtUnloadKeyExLogParam(POBJECT_ATTRIBUTES TargetKey, HANDLE Event)
{
    nlohmann::json param;
    param["TargetKey"] = appbox::ToJson(TargetKey);
    param["Event"] = appbox::PointerToString(Event);
    return param;
}

static appbox::LoggerF logger("NtUnloadKeyEx", NtUnloadKeyExLogParam);

static NTSTATUS Hook_NtUnloadKeyEx(POBJECT_ATTRIBUTES TargetKey, HANDLE Event)
{
    logger.Log(TargetKey, Event);
    return sys_NtUnloadKeyEx(TargetKey, Event);
}

static void LoadNtUnloadKeyEx()
{
    *appbox::HookNtUnloadKeyEx.ppPointer = GetProcAddress(appbox::sys.h_ntdll, appbox::HookNtUnloadKeyEx.name);
}

appbox::HookRecord appbox::HookNtUnloadKeyEx = {
    "NtUnloadKeyEx",
    LoadNtUnloadKeyEx,
    (void**)&sys_NtUnloadKeyEx,
    Hook_NtUnloadKeyEx,
};
