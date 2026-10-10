#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtLockRegistryKey.hpp"

T_NtLockRegistryKey sys_NtLockRegistryKey = nullptr;

static nlohmann::json NtLockRegistryKeyLogParam(HANDLE KeyHandle)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    return json;
}

static appbox::LoggerF logger("NtLockRegistryKey", NtLockRegistryKeyLogParam);

/**
 * @brief Detour of NtLockRegistryKey().
 *
 * The call locks the hive which holds the key, so the layer of the handle
 * decides the answer: the hive is the state of the sandbox, so a lock of a key
 * of the hive acts on the hive of the sandbox and the call is forwarded (the
 * kernel asks for a privilege the caller may not hold, which is its own
 * answer), while the hive of the host is not the state of the sandbox, so the
 * call is refused. A handle outside the root keys of the view belongs to no
 * layer of the view and is forwarded unchanged.
 */
static NTSTATUS Hook_NtLockRegistryKey(HANDLE KeyHandle)
{
    logger.Log(KeyHandle);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::RealHandle)
    {
        return STATUS_ACCESS_DENIED;
    }

    return sys_NtLockRegistryKey(KeyHandle);
}

static void LoadNtLockRegistryKey()
{
    sys_NtLockRegistryKey =
        reinterpret_cast<T_NtLockRegistryKey>(GetProcAddress(appbox::sys.h_ntdll, "NtLockRegistryKey"));
}

appbox::HookRecord appbox::HookNtLockRegistryKey = {
    "NtLockRegistryKey",
    LoadNtLockRegistryKey,
    (void**)&sys_NtLockRegistryKey,
    Hook_NtLockRegistryKey,
};
