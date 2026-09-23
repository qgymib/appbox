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
 * The call is delegated to the open policy of the isolation mode of the key
 * (appbox::registry::Hive::OpenIsolatedKey): the key is answered from the
 * sandbox hive first, and the mode decides what happens to a key the hive does
 * not hold — the host entry stays invisible (`Full`, `Hide`), is read through
 * (`WriteCopy` read access) or is copied up into the hive (`WriteCopy` write
 * access). The decision table lives in registry/IsolationPolicy.hpp.
 *
 * Paths outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS Hook_NtOpenKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes);

    std::wstring view_path;
    std::wstring relative;
    if (appbox::registry::Hive::MapKeyPath(ObjectAttributes, view_path, relative) == appbox::registry::HiveMap::Isolated)
    {
        return appbox::registry::Hive::OpenIsolatedKey(view_path, relative, DesiredAccess, ObjectAttributes->Attributes,
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
