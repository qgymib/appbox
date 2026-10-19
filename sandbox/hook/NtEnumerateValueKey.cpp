#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "registry/KeyGuard.hpp"
#include "utils/Log.hpp"
#include "NtEnumerateValueKey.hpp"

T_NtEnumerateValueKey sys_NtEnumerateValueKey = nullptr;

static nlohmann::json NtEnumerateValueKeyLogParam(HANDLE KeyHandle, ULONG Index,
                                                  KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                                  PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["Index"] = Index;
    json["KeyValueInformationClass"] = KeyValueInformationClass;
    json["KeyValueInformation"] = appbox::PointerToString(KeyValueInformation);
    json["Length"] = Length;
    json["ResultLength"] = appbox::PointerToString(ResultLength);
    return json;
}

static appbox::LoggerF logger("NtEnumerateValueKey", NtEnumerateValueKeyLogParam);

/**
 * @brief Detour of NtEnumerateValueKey().
 *
 * Value enumeration below a handle of a sandbox layer presents the merged view
 * of the three layers: the values of the state hive first, then the values of
 * the image which no higher layer shadows, then the values of the real key
 * which the view path addresses and which no sandbox layer shadows. Together
 * with the read through of NtQueryValueKey the enumeration is consistent: every
 * enumerated value can be queried.
 *
 * Handles which do not point into a sandbox layer are forwarded unchanged: a
 * host fallback handle only exposes the real layer anyway.
 */
static NTSTATUS NTAPI Hook_NtEnumerateValueKey(HANDLE KeyHandle, ULONG Index,
                                               KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                               PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (!appbox::registry::IsSandboxHandle(view))
    {
        return sys_NtEnumerateValueKey(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length,
                                       ResultLength);
    }

    HANDLE layer_handle = nullptr;
    bool   owned = false;
    ULONG  layer_index = 0;

    /* The state hive handle of the caller answers its own layer; a handle of the
     * image leaves it null, so the merge opens that layer itself. */
    const auto resolve = appbox::registry::Hive::ResolveMergedIndex(
        view == appbox::registry::HandleView::HiveHandle ? KeyHandle : nullptr, view_path, true, Index, layer_handle,
        owned, layer_index);
    appbox::registry::KeyGuard layer_guard(owned ? layer_handle : nullptr);

    switch (resolve)
    {
    case appbox::registry::MergedResolve::NoMoreEntries:
        return STATUS_NO_MORE_ENTRIES;

    case appbox::registry::MergedResolve::Error:
        /* The layers could not be collected, degrade to the single layer call. */
        return sys_NtEnumerateValueKey(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length,
                                       ResultLength);

    case appbox::registry::MergedResolve::HiveLayer:
    case appbox::registry::MergedResolve::ImageLayer:
    case appbox::registry::MergedResolve::RealLayer:
        return sys_NtEnumerateValueKey(layer_handle, layer_index, KeyValueInformationClass, KeyValueInformation, Length,
                                       ResultLength);

    default:
        return sys_NtEnumerateValueKey(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length,
                                       ResultLength);
    }
}

static void LoadNtEnumerateValueKey()
{
    sys_NtEnumerateValueKey =
        reinterpret_cast<T_NtEnumerateValueKey>(GetProcAddress(appbox::sys.h_ntdll, "NtEnumerateValueKey"));
}

appbox::HookRecord appbox::HookNtEnumerateValueKey = {
    "NtEnumerateValueKey",
    LoadNtEnumerateValueKey,
    (void**)&sys_NtEnumerateValueKey,
    Hook_NtEnumerateValueKey,
};
