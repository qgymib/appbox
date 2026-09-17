#include "utils/WinAPI.h" /* Must be first include file */
#include "registry/__init__.hpp"
#include "registry/KeyGuard.hpp"
#include "utils/Log.hpp"
#include "NtEnumerateKey.hpp"

T_NtEnumerateKey sys_NtEnumerateKey = nullptr;

static nlohmann::json NtEnumerateKeyLogParam(HANDLE KeyHandle, ULONG Index, KEY_INFORMATION_CLASS KeyInformationClass,
                                             PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    nlohmann::json json;
    json["KeyHandle"]            = appbox::PointerToString(KeyHandle);
    json["Index"]                = Index;
    json["KeyInformationClass"]  = KeyInformationClass;
    json["KeyInformation"]       = appbox::PointerToString(KeyInformation);
    json["Length"]               = Length;
    json["ResultLength"]         = appbox::PointerToString(ResultLength);
    return json;
}

static appbox::LoggerF logger("NtEnumerateKey", NtEnumerateKeyLogParam);

/**
 * @brief Detour of NtEnumerateKey().
 *
 * Enumeration below a hive handle presents the merged view of the two layers:
 * the sub keys of the hive layer first, followed by the sub keys of the real
 * key which the view path addresses and which are not shadowed by a hive key
 * of the same name. Sub key names are relative names, so the entries of the
 * real layer are returned unchanged.
 *
 * Handles which do not point into the hive are forwarded unchanged: a real
 * fallback handle only exposes the real layer anyway.
 */
static NTSTATUS Hook_NtEnumerateKey(HANDLE KeyHandle, ULONG Index, KEY_INFORMATION_CLASS KeyInformationClass,
                                    PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);

    std::wstring view_path;
    if (appbox::registry::Hive::MapHandleView(KeyHandle, view_path) != appbox::registry::HandleView::HiveHandle)
    {
        return sys_NtEnumerateKey(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);
    }

    HANDLE real            = nullptr;
    ULONG  layer_index     = 0;
    auto   resolve         = appbox::registry::Hive::ResolveMergedIndex(KeyHandle, view_path, false, Index, real,
                                                                        layer_index);
    appbox::registry::KeyGuard real_guard(real);

    switch (resolve)
    {
    case appbox::registry::MergedResolve::NoMoreEntries:
        return STATUS_NO_MORE_ENTRIES;

    case appbox::registry::MergedResolve::Error:
        /* The layers could not be collected, degrade to the single layer call. */
        return sys_NtEnumerateKey(KeyHandle, Index, KeyInformationClass, KeyInformation, Length, ResultLength);

    case appbox::registry::MergedResolve::HiveLayer:
        return sys_NtEnumerateKey(KeyHandle, layer_index, KeyInformationClass, KeyInformation, Length, ResultLength);

    case appbox::registry::MergedResolve::RealLayer:
        return sys_NtEnumerateKey(real_guard.get(), layer_index, KeyInformationClass, KeyInformation, Length,
                                  ResultLength);

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
