#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtOpenKeyEx.hpp"

T_NtOpenKeyEx sys_NtOpenKeyEx = nullptr;

static nlohmann::json NtOpenKeyExLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                          POBJECT_ATTRIBUTES ObjectAttributes, ULONG OpenOptions)
{
    nlohmann::json json;
    json["KeyHandle"]     = appbox::PointerToString(KeyHandle);
    json["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    json["OpenOptions"]       = OpenOptions;
    return json;
}

static appbox::LoggerF logger("NtOpenKeyEx", NtOpenKeyExLogParam);

/**
 * @brief Detour of NtOpenKeyEx().
 *
 * Same redirection as Hook_NtOpenKey(), with the open options forwarded.
 * RegOpenKeyExW() reaches this entry point on current Windows versions.
 */
static NTSTATUS Hook_NtOpenKeyEx(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 ULONG OpenOptions)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions);

    std::wstring view_path;
    std::wstring relative;
    if (appbox::registry::Hive::MapKeyPath(ObjectAttributes, view_path, relative) == appbox::registry::HiveMap::Isolated)
    {
        HANDLE key = nullptr;
        NTSTATUS st =
            appbox::registry::Hive::OpenKeyEx(relative, DesiredAccess, ObjectAttributes->Attributes,
                                              ObjectAttributes->SecurityDescriptor,
                                              ObjectAttributes->SecurityQualityOfService, OpenOptions, &key);
        if (NT_SUCCESS(st))
        {
            *KeyHandle = key;
            return st;
        }

        return appbox::registry::Hive::OpenRealKeyEx(view_path, DesiredAccess, ObjectAttributes->Attributes,
                                                     ObjectAttributes->SecurityDescriptor,
                                                     ObjectAttributes->SecurityQualityOfService, OpenOptions,
                                                     KeyHandle);
    }

    return sys_NtOpenKeyEx(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions);
}

static void LoadNtOpenKeyEx()
{
    sys_NtOpenKeyEx = reinterpret_cast<T_NtOpenKeyEx>(GetProcAddress(appbox::sys.h_ntdll, "NtOpenKeyEx"));
}

appbox::HookRecord appbox::HookNtOpenKeyEx = {
    "NtOpenKeyEx",
    LoadNtOpenKeyEx,
    (void**)&sys_NtOpenKeyEx,
    Hook_NtOpenKeyEx,
};
