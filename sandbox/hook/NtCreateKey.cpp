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
 * Every key creation below HKCU is redirected into the sandbox hive; the real
 * registry is never modified. NtCreateKey creates the intermediate keys of
 * the relative path automatically and reports through Disposition whether the
 * key was created or already existed inside the hive.
 */
static NTSTATUS Hook_NtCreateKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions, PULONG Disposition)
{
    logger.Log(KeyHandle, DesiredAccess, ObjectAttributes, TitleIndex, Class, CreateOptions, Disposition);

    std::wstring view_path;
    std::wstring relative;
    if (appbox::registry::Hive::MapKeyPath(ObjectAttributes, view_path, relative) == appbox::registry::HiveMap::Isolated)
    {
        return appbox::registry::Hive::CreateKey(relative, DesiredAccess, ObjectAttributes->Attributes,
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
