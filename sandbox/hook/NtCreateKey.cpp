#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtCreateKey.hpp"

T_NtCreateKey sys_NtCreateKey = nullptr;

static nlohmann::json NtCreateKeyLogParam(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
                                          POBJECT_ATTRIBUTES ObjectAttributes, ULONG TitleIndex,
                                          PUNICODE_STRING Class, ULONG CreateOptions, PULONG Disposition)
{
    nlohmann::json json;
    json["KeyHandle"]     = appbox::PointerToString(KeyHandle);
    json["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes);
    json["TitleIndex"]        = TitleIndex;
    json["Class"]              = appbox::ToJson(Class);
    json["CreateOptions"]      = CreateOptions;
    json["Disposition"]        = appbox::PointerToString(Disposition);
    return json;
}

static appbox::LoggerF logger("NtCreateKey", NtCreateKeyLogParam);

/**
 * @brief Detour of NtCreateKey().
 *
 * Every isolated create lands in the sandbox hive: the call is delegated to
 * appbox::registry::Hive::CreateIsolatedKey, which walks the path component by
 * component, creates the intermediate keys inside the hive as well and reports
 * through Disposition whether the key exists in the merged view of the caller
 * (see appbox::registry::ViewCreateDisposition). The isolation never modifies
 * the real registry, so the host key of the same name is left untouched — the
 * shadow key of the hive hides it, which is the behaviour of `WriteCopy` and
 * of `Full` and `Hide` alike.
 */
static NTSTATUS Hook_NtCreateKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions, PULONG Disposition)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions, Disposition);

    std::wstring view_path;
    std::wstring relative;
    if (appbox::registry::Hive::MapKeyPath(ObjectAttributes, view_path, relative) == appbox::registry::HiveMap::Isolated)
    {
        return appbox::registry::Hive::CreateIsolatedKey(view_path, relative, DesiredAccess,
                                                        ObjectAttributes->Attributes,
                                                        ObjectAttributes->SecurityDescriptor,
                                                        ObjectAttributes->SecurityQualityOfService, TitleIndex, Class,
                                                        CreateOptions, KeyHandle, Disposition);
    }

    return sys_NtCreateKey(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions, Disposition);
}

static void LoadNtCreateKey()
{
    sys_NtCreateKey = reinterpret_cast<T_NtCreateKey>(GetProcAddress(appbox::sys.h_ntdll, "NtCreateKey"));
}

appbox::HookRecord appbox::HookNtCreateKey = {
    "NtCreateKey",
    LoadNtCreateKey,
    (void**)&sys_NtCreateKey,
    Hook_NtCreateKey,
};
