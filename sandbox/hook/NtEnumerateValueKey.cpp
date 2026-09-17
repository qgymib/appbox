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
    json["KeyHandle"]                = appbox::PointerToString(KeyHandle);
    json["Index"]                    = Index;
    json["KeyValueInformationClass"] = KeyValueInformationClass;
    json["KeyValueInformation"]      = appbox::PointerToString(KeyValueInformation);
    json["Length"]                   = Length;
    json["ResultLength"]             = appbox::PointerToString(ResultLength);
    return json;
}

static appbox::LoggerF logger("NtEnumerateValueKey", NtEnumerateValueKeyLogParam);

/**
 * @brief Detour of NtEnumerateValueKey().
 *
 * Value enumeration below a hive handle presents the merged view of the two
 * layers: the values of the hive layer first, followed by the values of the
 * real key which the view path addresses and which are not shadowed by a hive
 * value of the same name. Together with the read through of NtQueryValueKey
 * the enumeration is consistent: every enumerated value can be queried.
 *
 * Handles which do not point into the hive are forwarded unchanged: a real
 * fallback handle only exposes the real layer anyway.
 */
static NTSTATUS Hook_NtEnumerateValueKey(HANDLE KeyHandle, ULONG Index,
                                         KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                                         PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    logger.Log(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);

    std::wstring view_path;
    if (appbox::registry::Hive::MapHandleView(KeyHandle, view_path) != appbox::registry::HandleView::HiveHandle)
    {
        return sys_NtEnumerateValueKey(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length,
                                       ResultLength);
    }

    HANDLE real        = nullptr;
    ULONG  layer_index = 0;
    auto   resolve     = appbox::registry::Hive::ResolveMergedIndex(KeyHandle, view_path, true, Index, real,
                                                                    layer_index);
    appbox::registry::KeyGuard real_guard(real);

    switch (resolve)
    {
    case appbox::registry::MergedResolve::NoMoreEntries:
        return STATUS_NO_MORE_ENTRIES;

    case appbox::registry::MergedResolve::Error:
        /* The layers could not be collected, degrade to the single layer call. */
        return sys_NtEnumerateValueKey(KeyHandle, Index, KeyValueInformationClass, KeyValueInformation, Length,
                                       ResultLength);

    case appbox::registry::MergedResolve::HiveLayer:
        return sys_NtEnumerateValueKey(KeyHandle, layer_index, KeyValueInformationClass, KeyValueInformation, Length,
                                       ResultLength);

    case appbox::registry::MergedResolve::RealLayer:
        return sys_NtEnumerateValueKey(real_guard.get(), layer_index, KeyValueInformationClass, KeyValueInformation,
                                       Length, ResultLength);

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
