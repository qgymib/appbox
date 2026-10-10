#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "utils/Log.hpp"
#include "NtSetInformationKey.hpp"

T_NtSetInformationKey sys_NtSetInformationKey = nullptr;

static nlohmann::json NtSetInformationKeyLogParam(HANDLE KeyHandle, KEY_SET_INFORMATION_CLASS KeySetInformationClass,
                                                  PVOID KeySetInformation, ULONG KeySetInformationLength)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["KeySetInformationClass"] = KeySetInformationClass;
    json["KeySetInformation"] = appbox::PointerToString(KeySetInformation);
    json["KeySetInformationLength"] = KeySetInformationLength;
    return json;
}

static appbox::LoggerF logger("NtSetInformationKey", NtSetInformationKeyLogParam);

/**
 * @brief Detour of NtSetInformationKey().
 *
 * The call changes a property of the key its handle denotes, so the layer of
 * the handle decides the answer: the hive is the write layer of the sandbox, so
 * a property of a key of the hive is a property of the sandbox and the call is
 * forwarded, while the object of the host layer is not the object of the
 * sandbox, so the call is refused and never reaches the real registry. A handle
 * outside the root keys of the view belongs to no layer of the view and is
 * forwarded unchanged.
 *
 * The refusal matches the one of NtDeleteKey, which refuses a handle of the
 * host layer the same way: a handle which the isolation handed out for a read
 * access open must not become the right to change the key of the host.
 */
static NTSTATUS Hook_NtSetInformationKey(HANDLE KeyHandle, KEY_SET_INFORMATION_CLASS KeySetInformationClass,
                                         PVOID KeySetInformation, ULONG KeySetInformationLength)
{
    logger.Log(KeyHandle, KeySetInformationClass, KeySetInformation, KeySetInformationLength);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (view == appbox::registry::HandleView::RealHandle)
    {
        return STATUS_ACCESS_DENIED;
    }

    return sys_NtSetInformationKey(KeyHandle, KeySetInformationClass, KeySetInformation, KeySetInformationLength);
}

static void LoadNtSetInformationKey()
{
    sys_NtSetInformationKey =
        reinterpret_cast<T_NtSetInformationKey>(GetProcAddress(appbox::sys.h_ntdll, "NtSetInformationKey"));
}

appbox::HookRecord appbox::HookNtSetInformationKey = {
    "NtSetInformationKey",
    LoadNtSetInformationKey,
    (void**)&sys_NtSetInformationKey,
    Hook_NtSetInformationKey,
};
