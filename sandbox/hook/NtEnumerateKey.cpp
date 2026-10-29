#include "WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "registry/KeyGuard.hpp"
#include "utils/Log.hpp"
#include "NtEnumerateKey.hpp"

T_NtEnumerateKey sys_NtEnumerateKey = nullptr;

static nlohmann::json NtEnumerateKeyLogParam(HANDLE KeyHandle, ULONG Index, KEY_INFORMATION_CLASS KeyInformationClass,
                                             PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    nlohmann::json json;
    json["KeyHandle"] = appbox::PointerToString(KeyHandle);
    json["Index"] = Index;
    json["KeyInformationClass"] = KeyInformationClass;
    json["KeyInformation"] = appbox::PointerToString(KeyInformation);
    json["Length"] = Length;
    json["ResultLength"] = appbox::PointerToString(ResultLength);
    return json;
}

static appbox::LoggerF logger("NtEnumerateKey", NtEnumerateKeyLogParam);

/**
 * @brief Detour of NtEnumerateKey().
 *
 * Enumeration below a handle of a sandbox layer presents the merged view of the
 * three layers: the sub keys of the state hive first, then the sub keys of the
 * image which no higher layer shadows, then the sub keys of the real key which
 * the view path addresses and which no sandbox layer shadows. Sub key names are
 * relative names, so the entries of every layer are returned unchanged.
 *
 * Handles which do not point into a sandbox layer are forwarded unchanged: a
 * host fallback handle only exposes the real layer anyway.
 */
static NTSTATUS NTAPI Hook_NtEnumerateKey(HANDLE KeyHandle, ULONG Index, KEY_INFORMATION_CLASS KeyInformationClass,
                                          PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);

    std::wstring                       view_path;
    const appbox::registry::HandleView view = appbox::registry::Hive::MapHandleView(KeyHandle, view_path);
    if (!appbox::registry::IsSandboxHandle(view))
    {
        return sys_NtEnumerateKey(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);
    }

    HANDLE layer_handle = nullptr;
    bool   owned = false;
    ULONG  layer_index = 0;

    /* The state hive handle of the caller answers its own layer; a handle of the
     * image leaves it null, so the merge opens that layer itself. */
    const auto resolve = appbox::registry::Hive::ResolveMergedIndex(
        view == appbox::registry::HandleView::HiveHandle ? KeyHandle : nullptr, view_path, false, Index, layer_handle,
        owned, layer_index);
    appbox::registry::KeyGuard layer_guard(owned ? layer_handle : nullptr);

    switch (resolve)
    {
    case appbox::registry::MergedResolve::NoMoreEntries:
        return STATUS_NO_MORE_ENTRIES;

    case appbox::registry::MergedResolve::Error:
        /* The layers could not be collected, degrade to the single layer call. */
        return sys_NtEnumerateKey(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);

    case appbox::registry::MergedResolve::HiveLayer:
    case appbox::registry::MergedResolve::ImageLayer:
    case appbox::registry::MergedResolve::RealLayer:
        return sys_NtEnumerateKey(layer_handle, layer_index, KeyInformationClass, KeyInformation, Length, ResultLength);

    default:
        return sys_NtEnumerateKey(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);
    }
}

static void LoadNtEnumerateKey()
{
    sys_NtEnumerateKey = reinterpret_cast<T_NtEnumerateKey>(GetProcAddress(appbox::sys.h_ntdll, "NtEnumerateKey"));
}

appbox::HookRecord appbox::HookNtEnumerateKey = {
    "NtEnumerateKey",
    LoadNtEnumerateKey,
    (void**)&sys_NtEnumerateKey,
    Hook_NtEnumerateKey,
};
