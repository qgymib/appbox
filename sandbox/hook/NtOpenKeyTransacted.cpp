#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtOpenKeyTransacted.hpp"

T_NtOpenKeyTransacted sys_NtOpenKeyTransacted = nullptr;

static nlohmann::json NtOpenKeyTransactedLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                                  POBJECT_ATTRIBUTES ObjectAttributes, HANDLE TransactionHandle)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    json["TransactionHandle"] = appbox::PointerToString(TransactionHandle);
    return json;
}

static appbox::LoggerF logger("NtOpenKeyTransacted", NtOpenKeyTransactedLogParam);

/**
 * @brief Detour of NtOpenKeyTransacted().
 *
 * Same open policy as Hook_NtOpenKey()
 * (appbox::registry::Hive::OpenIsolatedKeyTransacted): the key is answered from
 * the sandbox hive first, and the mode decides what happens to a key the hive
 * does not hold. The handle of the hive is opened with the transaction of the
 * caller, so a key the caller creates or changes through it is committed or
 * rolled back with that transaction — inside the sandbox, because the hive is
 * the write layer of the sandbox.
 *
 * A key which the hive does not hold and which the mode keeps visible is not
 * read through: a handle of the host layer would enlist the real hive into the
 * transaction of the caller, which is why the isolation refuses that case (see
 * appbox::registry::FallbackForKeyTransacted). Paths outside the root keys of
 * the view are forwarded unchanged, with the transaction of the caller.
 */
static NTSTATUS Hook_NtOpenKeyTransacted(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                         POBJECT_ATTRIBUTES ObjectAttributes, HANDLE TransactionHandle)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, TransactionHandle);

    std::wstring view_path;
    std::wstring relative;
    if (appbox::registry::Hive::MapKeyPath(ObjectAttributes, view_path, relative) ==
        appbox::registry::HiveMap::Isolated)
    {
        return appbox::registry::Hive::OpenIsolatedKeyTransacted(
            view_path, relative, DesiredAccess, ObjectAttributes->Attributes, ObjectAttributes->SecurityDescriptor,
            ObjectAttributes->SecurityQualityOfService, TransactionHandle, KeyHandle);
    }

    return sys_NtOpenKeyTransacted(KeyHandle, DesiredAccess, ObjectAttributes, TransactionHandle);
}

static void LoadNtOpenKeyTransacted()
{
    sys_NtOpenKeyTransacted =
        reinterpret_cast<T_NtOpenKeyTransacted>(GetProcAddress(appbox::sys.h_ntdll, "NtOpenKeyTransacted"));
}

appbox::HookRecord appbox::HookNtOpenKeyTransacted = {
    "NtOpenKeyTransacted",
    LoadNtOpenKeyTransacted,
    (void**)&sys_NtOpenKeyTransacted,
    Hook_NtOpenKeyTransacted,
};
