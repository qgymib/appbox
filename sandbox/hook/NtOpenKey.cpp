#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtOpenKey.hpp"

T_NtOpenKey sys_NtOpenKey = nullptr;

static nlohmann::json NtOpenKeyLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                        POBJECT_ATTRIBUTES ObjectAttributes)
{
    nlohmann::json json;
    json["KeyHandle"]     = appbox::PointerToString(KeyHandle);
    json["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    return json;
}

static appbox::LoggerF logger("NtOpenKey", NtOpenKeyLogParam);

/**
 * @brief Detour of NtOpenKey().
 *
 * Opens the key inside the sandbox hive when the requested path is inside
 * HKCU and the hive holds it. Every other case (not HKCU, or the hive does not
 * hold the key) falls back to the real registry through the view path, so
 * reads see the real registry when the sandbox has no value of its own
 * (read through).
 */
static NTSTATUS Hook_NtOpenKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes);

    std::wstring view_path;
    std::wstring relative;
    if (appbox::registry::Hive::MapKeyPath(ObjectAttributes, view_path, relative) == appbox::registry::HiveMap::Isolated)
    {
        HANDLE key = nullptr;
        NTSTATUS st = appbox::registry::Hive::OpenKey(relative, DesiredAccess, ObjectAttributes->Attributes,
                                                      ObjectAttributes->SecurityDescriptor,
                                                      ObjectAttributes->SecurityQualityOfService, &key);
        if (NT_SUCCESS(st))
        {
            *KeyHandle = key;
            return st;
        }

        return appbox::registry::Hive::OpenRealKey(view_path, DesiredAccess, ObjectAttributes->Attributes,
                                                   ObjectAttributes->SecurityDescriptor,
                                                   ObjectAttributes->SecurityQualityOfService, KeyHandle);
    }

    return sys_NtOpenKey(KeyHandle, DesiredAccess, ObjectAttributes);
}

static void LoadNtOpenKey()
{
    sys_NtOpenKey = reinterpret_cast<T_NtOpenKey>(GetProcAddress(appbox::sys.h_ntdll, "NtOpenKey"));
}

appbox::HookRecord appbox::HookNtOpenKey = {
    "NtOpenKey",
    LoadNtOpenKey,
    (void**)&sys_NtOpenKey,
    Hook_NtOpenKey,
};
