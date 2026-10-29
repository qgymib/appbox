#include "WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtFlushKey.hpp"

T_NtFlushKey sys_NtFlushKey = nullptr;

static nlohmann::json NtFlushKeyLogParam(HANDLE KeyHandle)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    return json;
}

static appbox::LoggerF logger("NtFlushKey", NtFlushKeyLogParam);

/**
 * @brief Detour of NtFlushKey().
 *
 * The flush writes the hive which holds the key to disk, so the layer of the
 * handle decides the answer: the state hive and the image are files of the
 * sandbox, so a flush of a key of either layer persists a file of the sandbox
 * and the call is forwarded — a flush changes no entry of the view, which is why
 * the read-only image is forwarded as well — while the hive of the host is not
 * the state of the sandbox, so the call is refused and never writes a file of
 * the host. A handle outside the root keys of the view belongs to no layer of
 * the view and is forwarded unchanged.
 */
static NTSTATUS NTAPI Hook_NtFlushKey(HANDLE KeyHandle)
{
    logger.Log(KeyHandle);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::RealHandle)
    {
        return STATUS_ACCESS_DENIED;
    }

    return sys_NtFlushKey(KeyHandle);
}

static void LoadNtFlushKey()
{
    sys_NtFlushKey = reinterpret_cast<T_NtFlushKey>(GetProcAddress(appbox::sys.h_ntdll, "NtFlushKey"));
}

appbox::HookRecord appbox::HookNtFlushKey = {
    "NtFlushKey",
    LoadNtFlushKey,
    (void**)&sys_NtFlushKey,
    Hook_NtFlushKey,
};
