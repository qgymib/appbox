#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtCreateKeyTransacted.hpp"

T_NtCreateKeyTransacted sys_NtCreateKeyTransacted = nullptr;

static nlohmann::json NtCreateKeyTransactedLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                                    POBJECT_ATTRIBUTES ObjectAttributes, ULONG TitleIndex,
                                                    PUNICODE_STRING Class, ULONG CreateOptions,
                                                    HANDLE TransactionHandle, PULONG Disposition)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    json["TitleIndex"] = TitleIndex;
    json["Class"] = appbox::ToJson(Class);
    json["CreateOptions"] = CreateOptions;
    json["TransactionHandle"] = appbox::PointerToString(TransactionHandle);
    json["Disposition"] = appbox::PointerToString(Disposition);
    return json;
}

static appbox::LoggerF logger("NtCreateKeyTransacted", NtCreateKeyTransactedLogParam);

/**
 * @brief Detour of NtCreateKeyTransacted().
 *
 * Every isolated create lands in the sandbox hive: the call is delegated to
 * appbox::registry::Hive::CreateIsolatedKeyTransacted, which walks the path
 * component by component and creates the key and its intermediate keys with the
 * transaction of the caller. A rollback therefore removes the key the create
 * added, which is what the caller of a transacted create expects, while the
 * real registry is never touched — the shadow key of the hive hides the host
 * key of the same name, which is the behaviour of `WriteCopy` and of `Full` and
 * `Hide` alike.
 *
 * A call which passes no transaction is answered by the plain create policy:
 * there is no transaction to keep, so the entry point behaves like NtCreateKey.
 */
static NTSTATUS Hook_NtCreateKeyTransacted(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                           POBJECT_ATTRIBUTES ObjectAttributes, ULONG TitleIndex, PUNICODE_STRING Class,
                                           ULONG CreateOptions, HANDLE TransactionHandle, PULONG Disposition)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions, TransactionHandle,
               Disposition);

    std::wstring view_path;
    std::wstring relative;
    if (appbox::registry::Hive::MapKeyPath(ObjectAttributes, view_path, relative) ==
        appbox::registry::HiveMap::Isolated)
    {
        return appbox::registry::Hive::CreateIsolatedKeyTransacted(
            view_path, relative, DesiredAccess, ObjectAttributes->Attributes, ObjectAttributes->SecurityDescriptor,
            ObjectAttributes->SecurityQualityOfService, TitleIndex, Class, CreateOptions, TransactionHandle, KeyHandle,
            Disposition);
    }

    return sys_NtCreateKeyTransacted(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions,
                                     TransactionHandle, Disposition);
}

static void LoadNtCreateKeyTransacted()
{
    sys_NtCreateKeyTransacted =
        reinterpret_cast<T_NtCreateKeyTransacted>(GetProcAddress(appbox::sys.h_ntdll, "NtCreateKeyTransacted"));
}

appbox::HookRecord appbox::HookNtCreateKeyTransacted = {
    "NtCreateKeyTransacted",
    LoadNtCreateKeyTransacted,
    (void**)&sys_NtCreateKeyTransacted,
    Hook_NtCreateKeyTransacted,
};
