#include "WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtSetValueKey.hpp"

T_NtSetValueKey sys_NtSetValueKey = nullptr;

static nlohmann::json NtSetValueKeyLogParam(HANDLE KeyHandle, PUNICODE_STRING ValueName, ULONG TitleIndex, ULONG Type,
                                            PVOID Data, ULONG DataSize)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["ValueName"] = appbox::ToJson(ValueName);
    json["TitleIndex"] = TitleIndex;
    json["Type"] = Type;
    json["Data"] = appbox::PointerToString(Data);
    json["DataSize"] = DataSize;
    return json;
}

static appbox::LoggerF logger("NtSetValueKey", NtSetValueKeyLogParam);

/**
 * @brief Detour of NtSetValueKey().
 *
 * A value write is the only modification of an entry which cannot be decided at
 * the open: the layer a write lands in depends on the value and not on the key,
 * because `Merge` writes a value the sandbox does not hold into the real
 * registry and a value the sandbox holds into the sandbox. The call is therefore
 * delegated to appbox::registry::Hive::SetIsolatedValue, which resolves the
 * layer of the value and runs the write against the key of that layer.
 *
 * Every other mode writes the value into the sandbox, which is the layer the
 * handle of the caller already points at: the isolation re-routes the call only
 * when the layers disagree, so a value write of a sandboxed process costs no
 * extra call outside `Merge`.
 *
 * A call whose value name cannot be read is forwarded unchanged, because a hook
 * must never read outside the memory of the caller.
 *
 * Handles outside the root keys of the view are forwarded unchanged.
 */
static NTSTATUS NTAPI Hook_NtSetValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName, ULONG TitleIndex, ULONG Type,
                                         PVOID Data, ULONG DataSize)
{
    logger.Log(KeyHandle, ValueName, TitleIndex, Type, Data, DataSize);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::NotIsolated)
    {
        return sys_NtSetValueKey(KeyHandle, ValueName, TitleIndex, Type, Data, DataSize);
    }

    std::wstring relative;
    std::wstring value_name;
    if (!appbox::registry::Hive::HiveRelativePath(view_path, relative) ||
        !appbox::registry::ReadValueName(ValueName, value_name))
    {
        return sys_NtSetValueKey(KeyHandle, ValueName, TitleIndex, Type, Data, DataSize);
    }

    return appbox::registry::Hive::SetIsolatedValue(view, KeyHandle, view_path, relative, value_name, TitleIndex, Type,
                                                    Data, DataSize);
}

static void LoadNtSetValueKey()
{
    sys_NtSetValueKey = reinterpret_cast<T_NtSetValueKey>(GetProcAddress(appbox::sys.h_ntdll, "NtSetValueKey"));
}

appbox::HookRecord appbox::HookNtSetValueKey = {
    "NtSetValueKey",
    LoadNtSetValueKey,
    (void**)&sys_NtSetValueKey,
    Hook_NtSetValueKey,
};
