#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtOpenKeyTransactedEx.hpp"

T_NtOpenKeyTransactedEx sys_NtOpenKeyTransactedEx = nullptr;

static nlohmann::json NtOpenKeyTransactedExLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                                    POBJECT_ATTRIBUTES ObjectAttributes, ULONG OpenOptions,
                                                    HANDLE TransactionHandle)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    json["OpenOptions"] = OpenOptions;
    json["TransactionHandle"] = appbox::PointerToString(TransactionHandle);
    return json;
}

static appbox::LoggerF logger("NtOpenKeyTransactedEx", NtOpenKeyTransactedExLogParam);

/**
 * @brief Detour of NtOpenKeyTransactedEx().
 *
 * Same open policy as Hook_NtOpenKeyTransacted()
 * (appbox::registry::Hive::OpenIsolatedKeyTransactedEx), with the open options
 * of the caller forwarded to every attempt. RegOpenKeyTransactedW() reaches
 * this entry point on current Windows versions.
 */
static NTSTATUS Hook_NtOpenKeyTransactedEx(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                           POBJECT_ATTRIBUTES ObjectAttributes, ULONG OpenOptions,
                                           HANDLE TransactionHandle)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions, TransactionHandle);

    std::wstring view_path;
    std::wstring relative;
    if (appbox::registry::Hive::MapKeyPath(ObjectAttributes, view_path, relative) ==
        appbox::registry::HiveMap::Isolated)
    {
        return appbox::registry::Hive::OpenIsolatedKeyTransactedEx(
            view_path, relative, DesiredAccess, ObjectAttributes->Attributes, ObjectAttributes->SecurityDescriptor,
            ObjectAttributes->SecurityQualityOfService, OpenOptions, TransactionHandle, KeyHandle);
    }

    return sys_NtOpenKeyTransactedEx(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions, TransactionHandle);
}

static void LoadNtOpenKeyTransactedEx()
{
    sys_NtOpenKeyTransactedEx =
        reinterpret_cast<T_NtOpenKeyTransactedEx>(GetProcAddress(appbox::sys.h_ntdll, "NtOpenKeyTransactedEx"));
}

appbox::HookRecord appbox::HookNtOpenKeyTransactedEx = {
    "NtOpenKeyTransactedEx",
    LoadNtOpenKeyTransactedEx,
    (void**)&sys_NtOpenKeyTransactedEx,
    Hook_NtOpenKeyTransactedEx,
};
